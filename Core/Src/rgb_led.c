/* SPDX-License-Identifier: AGPL-3.0-or-later */
/**
  ******************************************************************************
  * @file    rgb_led.c
  * @brief   GPIO + TIM + DMA RGB LED driver (no PWM peripheral, no interrupts).
  *          STM32F0 port.
  *
  * Principle
  * ---------
  * The configured timer free-runs and raises an update (overflow) DMA
  * request at RGB_PWM_STEPS * refresh_hz. That request clocks a DMA1
  * channel, which circularly copies a 256-word pattern buffer into the LED
  * port's BSRR. Every word carries set bits (low half) and reset bits
  * (high half) for all three LED pins, so each timer tick atomically
  * repositions R, G and B. A channel whose logical intensity is N is
  * driven ACTIVE in slots 0..N-1 and INACTIVE in slots N..255 - i.e. an
  * N/256 duty cycle - without ever touching the timer's output-compare/PWM
  * hardware. For an active-low (common-anode) LED the active level is LOW;
  * the renderer swaps the set/reset masks so callers stay in logical
  * color space.
  *
  * MCU time budget: zero in steady state. The channel runs in circular
  * mode with no DMA or timer interrupt enabled; the NVIC never fires for
  * this driver. RGB_SetColor() costs one 256-iteration buffer rewrite
  * (~30 us at 48 MHz) and nothing else.
  *
  * F072 specifics: DMA request routing is fixed per channel. TIM16's
  * update request is remapped to DMA1_Channel4 via SYSCFG (the default
  * channel 3 is taken by USART3_RX in this project); TIM1_UP lives on
  * DMA1_Channel5. Any DMA channel can reach GPIO on the F0 bus matrix.
  ******************************************************************************
  */

#include "rgb_led.h"

/* Slot i drives each pin active while i < duty[channel]. */
static uint32_t rgb_pattern[RGB_PWM_STEPS];

static TIM_HandleTypeDef rgb_htim;
static DMA_HandleTypeDef rgb_hdma;

static struct
{
  GPIO_TypeDef *port;       /* common port of all three pins */
  uint16_t      pin_r;
  uint16_t      pin_g;
  uint16_t      pin_b;
  bool          active_low;
  uint8_t       r;
  uint8_t       g;
  uint8_t       b;
  bool          on;
} rgb_state;

#if RGB_USE_GAMMA
/* CIE-ish gamma 2.8 lookup: perceived brightness -> duty */
static const uint8_t rgb_gamma8[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255,
};
#endif

/* Map an 8-bit intensity to a slot count in 0..RGB_PWM_STEPS (full scale
 * = solid on, so full white really is 100% duty). The 8-bit gamma output
 * is rescaled to the configured step count. */
static uint32_t RGB_Duty(uint8_t value)
{
#if RGB_USE_GAMMA
  uint32_t duty8 = rgb_gamma8[value];
#else
  uint32_t duty8 = value;
#endif
  return (value == 255U) ? RGB_PWM_STEPS : ((duty8 * RGB_PWM_STEPS) >> 8);
}

/* Rebuild the BSRR pattern from rgb_state. Runs while DMA keeps streaming;
 * worst case one PWM frame shows a mixed old/new color, which is invisible
 * to the eye. For active-low hardware the "active" slot resets the pin
 * (BSRR high half) and the idle slot sets it. */
static void RGB_RenderPattern(void)
{
  const uint32_t duty_r = rgb_state.on ? RGB_Duty(rgb_state.r) : 0U;
  const uint32_t duty_g = rgb_state.on ? RGB_Duty(rgb_state.g) : 0U;
  const uint32_t duty_b = rgb_state.on ? RGB_Duty(rgb_state.b) : 0U;

  const uint32_t act_r  = rgb_state.active_low ? ((uint32_t)rgb_state.pin_r << 16)
                                               : (uint32_t)rgb_state.pin_r;
  const uint32_t idle_r = rgb_state.active_low ? (uint32_t)rgb_state.pin_r
                                               : ((uint32_t)rgb_state.pin_r << 16);
  const uint32_t act_g  = rgb_state.active_low ? ((uint32_t)rgb_state.pin_g << 16)
                                               : (uint32_t)rgb_state.pin_g;
  const uint32_t idle_g = rgb_state.active_low ? (uint32_t)rgb_state.pin_g
                                               : ((uint32_t)rgb_state.pin_g << 16);
  const uint32_t act_b  = rgb_state.active_low ? ((uint32_t)rgb_state.pin_b << 16)
                                               : (uint32_t)rgb_state.pin_b;
  const uint32_t idle_b = rgb_state.active_low ? (uint32_t)rgb_state.pin_b
                                               : ((uint32_t)rgb_state.pin_b << 16);

  for (uint32_t i = 0; i < RGB_PWM_STEPS; i++)
  {
    uint32_t word = 0;
    word |= (i < duty_r) ? act_r : idle_r;
    word |= (i < duty_g) ? act_g : idle_g;
    word |= (i < duty_b) ? act_b : idle_b;
    rgb_pattern[i] = word;
  }
}

/* Enable the AHB clock of an arbitrary GPIO port (F0 ports) */
static HAL_StatusTypeDef RGB_EnableGpioClock(const GPIO_TypeDef *port)
{
  if      (port == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
  else if (port == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
  else if (port == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
#ifdef GPIOD
  else if (port == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
#endif
#ifdef GPIOE
  else if (port == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
#endif
#ifdef GPIOF
  else if (port == GPIOF) { __HAL_RCC_GPIOF_CLK_ENABLE(); }
#endif
  else                    { return HAL_ERROR; }
  return HAL_OK;
}

/* Resolve the timer's update-request DMA channel, apply the SYSCFG remap
 * where needed, and enable the timer clock. STM32F072 fixed request map
 * (RM0091): TIM16_UP on DMA1_Channel3 (default, taken by USART3_RX here)
 * or DMA1_Channel4 via the TIM16 DMA remap; TIM1_UP on DMA1_Channel5.
 * Other timers map only to channels this project already uses. */
static HAL_StatusTypeDef RGB_ResolveTimer(const TIM_TypeDef *tim,
                                          DMA_Channel_TypeDef **channel)
{
  if (tim == TIM16)
  {
    __HAL_RCC_TIM16_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_DMA_REMAP_CHANNEL_ENABLE(DMA_REMAP_TIM16_DMA_CH4);
    *channel = DMA1_Channel4;
    return HAL_OK;
  }
  if (tim == TIM1)
  {
    __HAL_RCC_TIM1_CLK_ENABLE();
    *channel = DMA1_Channel5;
    return HAL_OK;
  }
  return HAL_ERROR;
}

/* Kernel clock of the pacing timer. The F0 has a single APB: PCLK1,
 * doubled when the APB prescaler is > 1. */
static uint32_t RGB_TimerClockHz(void)
{
  uint32_t pclk = HAL_RCC_GetPCLK1Freq();
  uint32_t ppre = (RCC->CFGR & RCC_CFGR_PPRE) >> RCC_CFGR_PPRE_Pos;
  return (ppre < 4U) ? pclk : pclk * 2U;
}

/* Structural validity of a config: all pointers present, all three pins on
 * one port (one DMA channel writes one BSRR register), and no pin shared
 * between channels. */
static bool RGB_ConfigValid(const RGB_Config *cfg)
{
  if ((cfg == NULL) || (cfg->timer == NULL))
  {
    return false;
  }
  if ((cfg->r_port == NULL) || (cfg->g_port == NULL) || (cfg->b_port == NULL))
  {
    return false;
  }
  if ((cfg->r_port != cfg->g_port) || (cfg->r_port != cfg->b_port))
  {
    return false;
  }
  if ((cfg->r_pin == 0U) || (cfg->g_pin == 0U) || (cfg->b_pin == 0U))
  {
    return false;
  }
  return ((cfg->r_pin & cfg->g_pin) == 0U) &&
         ((cfg->r_pin & cfg->b_pin) == 0U) &&
         ((cfg->g_pin & cfg->b_pin) == 0U);
}

/* Configure the three LED pins as outputs at their idle (LED-off) level */
static void RGB_SetupGpio(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin   = (uint32_t)rgb_state.pin_r | rgb_state.pin_g | rgb_state.pin_b;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(rgb_state.port, &gpio);
  HAL_GPIO_WritePin(rgb_state.port, (uint16_t)gpio.Pin,
                    rgb_state.active_low ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Timer: plain up-counting time base overflowing at STEPS * refresh_hz.
 *
 * ORDER MATTERS: this must run BEFORE the DMA channel is configured.
 * HAL_TIM_Base_Init() on a fresh handle invokes HAL_TIM_Base_MspInit(),
 * and the CubeMX-generated TIM16 branch there re-runs HAL_DMA_Init() on
 * this same channel with its own (periph-to-mem, halfword, one-shot)
 * settings - which would clobber ours if we configured the DMA first. */
static HAL_StatusTypeDef RGB_SetupTimer(TIM_TypeDef *tim, uint32_t refresh_hz)
{
  uint32_t refresh = (refresh_hz != 0U) ? refresh_hz : RGB_DEFAULT_REFRESH_HZ;
  uint32_t div     = RGB_TimerClockHz() / (RGB_PWM_STEPS * refresh);
  uint32_t psc     = div >> 16;                 /* 0 unless the clock is huge */

  if (div == 0U)
  {
    return HAL_ERROR;                           /* refresh_hz too fast */
  }

  rgb_htim.Instance               = tim;
  rgb_htim.Init.Prescaler         = psc;
  rgb_htim.Init.CounterMode       = TIM_COUNTERMODE_UP;
  rgb_htim.Init.Period            = (div / (psc + 1U)) - 1U;
  rgb_htim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  rgb_htim.Init.RepetitionCounter = 0;
  rgb_htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  return HAL_TIM_Base_Init(&rgb_htim);
}

/* DMA: memory-to-peripheral, word-wide, circular over the pattern buffer.
 * No interrupt is enabled - the channel never stops and needs no service.
 * Configured after the timer (see ordering note above) so the driver's
 * settings are the ones in force; it owns the channel from here on. */
static HAL_StatusTypeDef RGB_SetupDma(DMA_Channel_TypeDef *channel)
{
  rgb_hdma.Instance                 = channel;
  rgb_hdma.Init.Direction           = DMA_MEMORY_TO_PERIPH;
  rgb_hdma.Init.PeriphInc           = DMA_PINC_DISABLE;
  rgb_hdma.Init.MemInc              = DMA_MINC_ENABLE;
  rgb_hdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  rgb_hdma.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
  rgb_hdma.Init.Mode                = DMA_CIRCULAR;
  rgb_hdma.Init.Priority            = DMA_PRIORITY_LOW;
  if (HAL_DMA_Init(&rgb_hdma) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_DMA_Start(&rgb_hdma,
                       (uint32_t)rgb_pattern,
                       (uint32_t)&rgb_state.port->BSRR,
                       RGB_PWM_STEPS);
}

HAL_StatusTypeDef RGB_Init_Driver(const RGB_Config *cfg)
{
  DMA_Channel_TypeDef *channel;

  if (!RGB_ConfigValid(cfg))
  {
    return HAL_ERROR;
  }
  if (RGB_ResolveTimer(cfg->timer, &channel) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (RGB_EnableGpioClock(cfg->r_port) != HAL_OK)
  {
    return HAL_ERROR;
  }
  __HAL_RCC_DMA1_CLK_ENABLE();

  rgb_state.port       = cfg->r_port;
  rgb_state.pin_r      = cfg->r_pin;
  rgb_state.pin_g      = cfg->g_pin;
  rgb_state.pin_b      = cfg->b_pin;
  rgb_state.active_low = cfg->active_low;

  /* Start dark: state is off, pattern holds every pin's idle level */
  rgb_state.on = false;
  RGB_RenderPattern();
  RGB_SetupGpio();

  if (RGB_SetupTimer(cfg->timer, cfg->refresh_hz) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (RGB_SetupDma(channel) != HAL_OK)
  {
    return HAL_ERROR;
  }

  /* Route the update event to the DMA and let the timer free-run */
  __HAL_TIM_ENABLE_DMA(&rgb_htim, TIM_DMA_UPDATE);
  return HAL_TIM_Base_Start(&rgb_htim);
}

void RGB_SetColor(uint8_t r, uint8_t g, uint8_t b, bool on)
{
  rgb_state.r  = r;
  rgb_state.g  = g;
  rgb_state.b  = b;
  rgb_state.on = on;
  RGB_RenderPattern();
}

void RGB_GetColor(uint8_t *r, uint8_t *g, uint8_t *b, bool *on)
{
  if (r  != NULL) { *r  = rgb_state.r;  }
  if (g  != NULL) { *g  = rgb_state.g;  }
  if (b  != NULL) { *b  = rgb_state.b;  }
  if (on != NULL) { *on = rgb_state.on; }
}

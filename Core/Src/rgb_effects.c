/* SPDX-License-Identifier: AGPL-3.0-or-later */
/**
  ******************************************************************************
  * @file    rgb_effects.c
  * @brief   Timer-interrupt-driven animation layer on top of the rgb_led
  *          driver. STM32F0 port.
  *
  * The bound timer fires at RGB_EFFECT_RATE_HZ and RGB_EffectTimerStep()
  * advances the active effect from the ISR, so the application can block
  * (HAL_Delay, long computations) without disturbing an animation.
  *
  * The timer runs only while an effect is active: every effect setter arms
  * it, RGB_EffectStop() and a completed fade disarm it. Idle or static
  * color therefore costs zero interrupts, keeping the bare driver's
  * zero-MCU-time property.
  *
  * All math is integer-only. Brightness ramps use a triangle wave and rely
  * on the driver's gamma table to make them perceptually smooth.
  ******************************************************************************
  */

#include "rgb_effects.h"
#include "rgb_led.h"

typedef enum
{
  EFFECT_NONE = 0,
  EFFECT_FADE,
  EFFECT_BREATHE,
  EFFECT_RAINBOW,
  EFFECT_FLASH,
  EFFECT_CYCLE,
} EffectType;

static TIM_HandleTypeDef *fx_htim;

static struct
{
  volatile EffectType type;
  uint32_t   t0;         /* HAL tick when the effect (re)started            */
  uint32_t   duration;   /* fade length / period / cycle dwell time, ms     */
  uint8_t    from[3];    /* fade start color                                */
  uint8_t    to[3];      /* fade target / breathe / flash base color        */
  uint8_t    cycle[RGB_CYCLE_MAX_COLORS][3];   /* color-cycle list (copied) */
  uint8_t    cycle_count;
} fx;

/* Effective color currently on the LED (off counts as black), so fades
 * always start from reality even after direct RGB_SetColor() calls */
static void FX_Current(uint8_t rgb[3])
{
  bool on;

  RGB_GetColor(&rgb[0], &rgb[1], &rgb[2], &on);
  if (!on)
  {
    rgb[0] = 0;
    rgb[1] = 0;
    rgb[2] = 0;
  }
}

/* Timer arm/disarm. Setters run in thread context while the ISR may be
 * live, so every setter disarms first, mutates state, then re-arms. */
static void FX_Disarm(void)
{
  if (fx_htim != NULL)
  {
    HAL_TIM_Base_Stop_IT(fx_htim);
  }
}

static void FX_Arm(void)
{
  if (fx_htim != NULL)
  {
    /* Drop any update flag pending from init/EGR so the first ISR tick
     * happens one full period from now */
    __HAL_TIM_CLEAR_IT(fx_htim, TIM_IT_UPDATE);
    HAL_TIM_Base_Start_IT(fx_htim);
  }
}

/* Hand a color to the driver only when it differs from what is showing */
static void FX_Show(uint8_t r, uint8_t g, uint8_t b)
{
  uint8_t cur[3];

  FX_Current(cur);
  if ((r != cur[0]) || (g != cur[1]) || (b != cur[2]))
  {
    RGB_SetColor(r, g, b, true);
  }
}

/* Linear interpolation from -> to at progress num/den */
static uint8_t FX_Lerp(uint8_t from, uint8_t to, uint32_t num, uint32_t den)
{
  return (uint8_t)((int32_t)from
                   + (((int32_t)to - (int32_t)from) * (int32_t)num)
                     / (int32_t)den);
}

/* Classic 768-position hue wheel: red -> green -> blue -> red */
static void FX_HueToRgb(uint32_t hue, uint8_t rgb[3])
{
  uint8_t seg = (uint8_t)(hue >> 8);        /* 0..2   */
  uint8_t o   = (uint8_t)(hue & 0xFFU);     /* 0..255 */

  switch (seg)
  {
    case 0:  rgb[0] = 255U - o; rgb[1] = o;        rgb[2] = 0;        break;
    case 1:  rgb[0] = 0;        rgb[1] = 255U - o; rgb[2] = o;        break;
    default: rgb[0] = o;        rgb[1] = 0;        rgb[2] = 255U - o; break;
  }
}

HAL_StatusTypeDef RGB_EffectsInit(TIM_HandleTypeDef *htim)
{
  if (htim == NULL)
  {
    return HAL_ERROR;
  }

  /* Timer kernel clock: the F0 has a single APB - PCLK1, doubled when
   * the APB prescaler is > 1 */
  uint32_t pclk   = HAL_RCC_GetPCLK1Freq();
  uint32_t ppre   = (RCC->CFGR & RCC_CFGR_PPRE) >> RCC_CFGR_PPRE_Pos;
  uint32_t timclk = (ppre < 4U) ? pclk : pclk * 2U;

  uint32_t div = timclk / RGB_EFFECT_RATE_HZ;
  uint32_t psc = div >> 16;

  htim->Init.Prescaler         = psc;
  htim->Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim->Init.Period            = (div / (psc + 1U)) - 1U;
  htim->Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(htim) != HAL_OK)
  {
    return HAL_ERROR;
  }

  fx_htim = htim;   /* bound; timer stays stopped until an effect starts */
  return HAL_OK;
}

void RGB_EffectTimerStep(void)
{
  uint32_t elapsed = HAL_GetTick() - fx.t0;

  switch (fx.type)
  {
    case EFFECT_FADE:
    {
      if (elapsed >= fx.duration)
      {
        FX_Show(fx.to[0], fx.to[1], fx.to[2]);
        fx.type = EFFECT_NONE;
        FX_Disarm();                        /* target reached: back to 0 IRQ */
      }
      else
      {
        FX_Show(FX_Lerp(fx.from[0], fx.to[0], elapsed, fx.duration),
                FX_Lerp(fx.from[1], fx.to[1], elapsed, fx.duration),
                FX_Lerp(fx.from[2], fx.to[2], elapsed, fx.duration));
      }
      break;
    }

    case EFFECT_BREATHE:
    {
      /* Triangle 0..255..0 across the period; the driver's gamma turns
       * this into a perceptually even breathing curve. */
      uint32_t pos   = ((elapsed % fx.duration) * 511U) / fx.duration;
      uint32_t level = (pos < 256U) ? pos : (511U - pos);

      FX_Show((uint8_t)((fx.to[0] * level) / 255U),
              (uint8_t)((fx.to[1] * level) / 255U),
              (uint8_t)((fx.to[2] * level) / 255U));
      break;
    }

    case EFFECT_RAINBOW:
    {
      uint8_t  rgb[3];
      uint32_t hue = ((elapsed % fx.duration) * 767U) / fx.duration;

      FX_HueToRgb(hue, rgb);
      FX_Show(rgb[0], rgb[1], rgb[2]);
      break;
    }

    case EFFECT_FLASH:
    {
      /* On for the first half of every period, dark for the second */
      if ((elapsed % fx.duration) < (fx.duration / 2U))
      {
        FX_Show(fx.to[0], fx.to[1], fx.to[2]);
      }
      else
      {
        FX_Show(0, 0, 0);
      }
      break;
    }

    case EFFECT_CYCLE:
    {
      uint32_t idx = (elapsed / fx.duration) % fx.cycle_count;

      FX_Show(fx.cycle[idx][0], fx.cycle[idx][1], fx.cycle[idx][2]);
      break;
    }

    default:
      FX_Disarm();                          /* spurious tick with no effect */
      break;
  }
}

void RGB_FadeTo(uint8_t r, uint8_t g, uint8_t b, uint32_t ms)
{
  FX_Disarm();

  if (ms == 0U)
  {
    fx.type = EFFECT_NONE;
    FX_Show(r, g, b);
    return;
  }

  FX_Current(fx.from);
  fx.to[0]    = r;
  fx.to[1]    = g;
  fx.to[2]    = b;
  fx.duration = ms;
  fx.t0       = HAL_GetTick();
  fx.type     = EFFECT_FADE;
  FX_Arm();
}

void RGB_Breathe(uint8_t r, uint8_t g, uint8_t b, uint32_t period_ms)
{
  FX_Disarm();
  fx.to[0]    = r;
  fx.to[1]    = g;
  fx.to[2]    = b;
  fx.duration = (period_ms != 0U) ? period_ms : 1U;
  fx.t0       = HAL_GetTick();
  fx.type     = EFFECT_BREATHE;
  FX_Arm();
}

void RGB_Rainbow(uint32_t period_ms)
{
  FX_Disarm();
  fx.duration = (period_ms != 0U) ? period_ms : 1U;
  fx.t0       = HAL_GetTick();
  fx.type     = EFFECT_RAINBOW;
  FX_Arm();
}

void RGB_Flash(uint8_t r, uint8_t g, uint8_t b, uint32_t period_ms)
{
  FX_Disarm();
  fx.to[0]    = r;
  fx.to[1]    = g;
  fx.to[2]    = b;
  fx.duration = (period_ms >= 2U) ? period_ms : 2U;
  fx.t0       = HAL_GetTick();
  fx.type     = EFFECT_FLASH;
  FX_Arm();
}

void RGB_ColorCycle(const uint8_t colors[][3], uint8_t count,
                    uint32_t dwell_ms)
{
  FX_Disarm();

  if ((colors == NULL) || (count == 0U))
  {
    fx.type = EFFECT_NONE;
    return;
  }
  if (count > RGB_CYCLE_MAX_COLORS)
  {
    count = RGB_CYCLE_MAX_COLORS;
  }

  for (uint8_t i = 0; i < count; i++)
  {
    fx.cycle[i][0] = colors[i][0];
    fx.cycle[i][1] = colors[i][1];
    fx.cycle[i][2] = colors[i][2];
  }
  fx.cycle_count = count;
  fx.duration    = (dwell_ms != 0U) ? dwell_ms : 1U;
  fx.t0          = HAL_GetTick();
  fx.type        = EFFECT_CYCLE;
  FX_Arm();
}

void RGB_EffectStop(void)
{
  FX_Disarm();
  fx.type = EFFECT_NONE;
}

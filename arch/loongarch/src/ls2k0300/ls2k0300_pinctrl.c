/****************************************************************************
 * arch/loongarch/src/ls2k0300/ls2k0300_pinctrl.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>
#include <nuttx/pinctrl/pinctrl.h>

#include "loongarch_internal.h"
#include "hardware/ls2k0300_memorymap.h"
#include "hardware/ls2k0300_pinctrl.h"
#include "hardware/ls2k0300_gpio.h"

#if defined(CONFIG_LS2K0300_PINCTRL)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define LS2K0300_PINCTRL_FUNC_GPIO    0
#define LS2K0300_PINCTRL_FUNC_ALT1    1
#define LS2K0300_PINCTRL_FUNC_ALT2    2
#define LS2K0300_PINCTRL_FUNC_ALT3    3

#define PINS_PER_REG                 16

struct ls2k0300_pinctrl_dev_s
{
  struct pinctrl_dev_s dev;
  uintptr_t base;
};

/****************************************************************************
 * Name: ls2k0300_pinctrl_set_function
 *
 * Description:
 *   Select a pin alternate function using its two-bit mux field. Reject
 *   invalid pin and function numbers and preserve neighboring mux fields.
 *
 ****************************************************************************/

static int ls2k0300_pinctrl_set_function(FAR struct pinctrl_dev_s *dev,
                                        uint32_t pin, uint32_t function)
{
  FAR struct ls2k0300_pinctrl_dev_s *priv =
      (FAR struct ls2k0300_pinctrl_dev_s *)dev;
  uint32_t reg_offset;
  uint32_t bit_offset;
  uint32_t reg_val;
  uint32_t mask;

  if (pin >= LS2K0300_GPIO_NPINS || function > LS2K0300_PINCTRL_FUNC_MASK)
    {
      return -EINVAL;
    }

  reg_offset = (pin / PINS_PER_REG) * 4;
  bit_offset = (pin % PINS_PER_REG) * 2;

  mask = LS2K0300_PINCTRL_FUNC_MASK << bit_offset;

  reg_val = getreg32(priv->base + reg_offset);
  reg_val &= ~mask;
  reg_val |= (function & LS2K0300_PINCTRL_FUNC_MASK) << bit_offset;
  putreg32(reg_val, priv->base + reg_offset);

  return OK;
}

/****************************************************************************
 * Name: ls2k0300_pinctrl_set_strength
 *
 * Description:
 *   Return -ENOSYS because programmable drive strength is not implemented
 *   by this lower half.
 *
 ****************************************************************************/

static int ls2k0300_pinctrl_set_strength(FAR struct pinctrl_dev_s *dev,
                                        uint32_t pin, uint32_t strength)
{
  return -ENOSYS;
}

/****************************************************************************
 * Name: ls2k0300_pinctrl_set_driver
 *
 * Description:
 *   Return -ENOSYS because output driver type selection is not
 *   implemented by this lower half.
 *
 ****************************************************************************/

static int ls2k0300_pinctrl_set_driver(FAR struct pinctrl_dev_s *dev,
                                      uint32_t pin,
                                      enum pinctrl_drivertype_e type)
{
  return -ENOSYS;
}

/****************************************************************************
 * Name: ls2k0300_pinctrl_set_slewrate
 *
 * Description:
 *   Return -ENOSYS because slew-rate selection is not implemented by this
 *   lower half.
 *
 ****************************************************************************/

static int ls2k0300_pinctrl_set_slewrate(FAR struct pinctrl_dev_s *dev,
                                        uint32_t pin, uint32_t slewrate)
{
  return -ENOSYS;
}

/****************************************************************************
 * Name: ls2k0300_pinctrl_select_gpio
 *
 * Description:
 *   Select the GPIO mux function for one pin through the common mux
 *   helper.
 *
 ****************************************************************************/

static int ls2k0300_pinctrl_select_gpio(FAR struct pinctrl_dev_s *dev,
                                       uint32_t pin)
{
  return ls2k0300_pinctrl_set_function(dev, pin, LS2K0300_PINCTRL_FUNC_GPIO);
}

static const struct pinctrl_ops_s g_pinctrl_ops =
{
  .set_function = ls2k0300_pinctrl_set_function,
  .set_strength = ls2k0300_pinctrl_set_strength,
  .set_driver   = ls2k0300_pinctrl_set_driver,
  .set_slewrate = ls2k0300_pinctrl_set_slewrate,
  .select_gpio  = ls2k0300_pinctrl_select_gpio,
};

static struct ls2k0300_pinctrl_dev_s g_pinctrl_priv =
{
  .dev =
    {
      .ops = &g_pinctrl_ops,
    },
  .base = PHYS_TO_UNCACHED(LS2K0300_PINCTRL_BASE),
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ls2k0300_pinctrl_initialize
 *
 * Description:
 *   Register the static pin controller as controller zero. Return the
 *   registration error if the upper half cannot create the device.
 *
 ****************************************************************************/

int ls2k0300_pinctrl_initialize(void)
{
  int ret;

  ret = pinctrl_register(&g_pinctrl_priv.dev, 0);
  if (ret < 0)
    {
      _err("ERROR: pinctrl_register failed: %d\n", ret);
      return ret;
    }

  return OK;
}

/****************************************************************************
 * Name: ls2k0300_pinctrl_set_pinmux
 *
 * Description:
 *   Update the masked bits of a mux register at a controller-relative
 *   offset. Board initialization supplies valid offsets and serializes
 *   updates.
 *
 ****************************************************************************/

int ls2k0300_pinctrl_set_pinmux(uint32_t reg_offset, uint32_t value,
                               uint32_t mask)
{
  uint32_t reg_val;

  reg_val = getreg32(g_pinctrl_priv.base + reg_offset);
  reg_val &= ~mask;
  reg_val |= value & mask;
  putreg32(reg_val, g_pinctrl_priv.base + reg_offset);

  return OK;
}

/****************************************************************************
 * Name: ls2k0300_pinctrl_get_pinmux
 *
 * Description:
 *   Read masked mux bits from a controller-relative register offset.
 *
 ****************************************************************************/

uint32_t ls2k0300_pinctrl_get_pinmux(uint32_t reg_offset, uint32_t mask)
{
  return getreg32(g_pinctrl_priv.base + reg_offset) & mask;
}

#endif

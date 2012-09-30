/*
 * arch/arm/mach-tegra/tegra_pmqos.h
 *
 * Copyright (C) 2012 Paul Reioux (aka Faux123)
 *
 * Author:
 *	faux123 <reioux@gmail.com>
 *
 * History:
 *      -original version (Paul Reioux)
 *      -cleaned since oc was reworked (Dennis Rassmann)
 *      -added comment for T3_VARIANT_BOOST (Dennis Rassmann)
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* in kHz */
#define BOOST_CPU_FREQ_MIN	1400000
#define CAP_CPU_FREQ_MAX	475000

#define CPUFREQ_SCALING_MAX_FREQ	"BOOST_CPU_FREQ_MIN"

/* in 10 MHz
 * this effectively makes all phone 1700Mhz capable, but is limited by
 * the clock definitions in tegra_dvfs.c */
#define T3_VARIANT_BOOSTX       20
#define T3_VARIANT_BOOST0       10

extern unsigned int tegra_pmqos_boost_freq;
extern unsigned int tegra_pmqos_cap_freq;

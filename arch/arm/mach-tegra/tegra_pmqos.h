/*
 * arch/arm/mach-tegra/tegra_pmqos.h
 *
 * Copyright (C) 2012 Paul Reioux (aka Faux123)
 *
 * Author:
 *	faux123 <reioux@gmail.com>
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
#define BOOST_CPU_FREQ_MIN	1500000
#define CAP_CPU_FREQ_MAX	475000

#define CPUFREQ_SCALING_MAX_FREQ_V3	"1700000"
#define CPUFREQ_SCALING_MAX_FREQ_V2	"1600000"
#define CPUFREQ_SCALING_MAX_FREQ_V1	"1600000"
#define CPUFREQ_SCALING_MAX_FREQ_V0	"1500000"
#define CPUFREQ_SCALING_MAX_FREQ	"BOOST_CPU_FREQ_MIN"

/* in 10 MHz */
#define T3_VARIANT_3_BOOSTX	20
#define T3_VARIANT_2_BOOSTX	15
#define T3_VARIANT_1_BOOSTX	10

#define T3_VARIANT_3_BOOST0	10
#define T3_VARIANT_2_BOOST0	5
#define T3_VARIANT_1_BOOST0	0

#define GAMING_REDUCTION_FREQ	10

extern unsigned int tegra_pmqos_boost_freq;
extern unsigned int tegra_pmqos_cap_freq;

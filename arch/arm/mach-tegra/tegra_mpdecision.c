/*
 * arch/arm/mach-tegra/tegra_mpdecision.c
 *
 * cpu auto-hotplug/unplug based on system load for tegra quadcore cpus
 * low power single core while screen is off
 *
 * Copyright (c) 2012, Dennis Rassmann <showp1984@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/earlysuspend.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <asm-generic/cputime.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>

#define DEBUG 0

#define MPDEC_TAG                       "[MPDEC]: "
#define TEGRA_MPDEC_STARTDELAY            20000
#define TEGRA_MPDEC_DELAY                 100
#define TEGRA_MPDEC_PAUSE                 10000
#define TEGRA_MPDEC_IDLE_FREQ             640000

enum {
	TEGRA_MPDEC_DISABLED = 0,
	TEGRA_MPDEC_IDLE,
	TEGRA_MPDEC_DOWN,
	TEGRA_MPDEC_UP,
};

struct tegra_mpdec_cpudata_t {
	struct mutex suspend_mutex;
	int online;
	int device_suspended;
	cputime64_t on_time;
};
static DEFINE_PER_CPU(struct tegra_mpdec_cpudata_t, tegra_mpdec_cpudata);

static struct delayed_work tegra_mpdec_work;
static DEFINE_MUTEX(tegra_cpu_lock);

static struct tegra_mpdec_tuners {
	unsigned int startdelay;
	unsigned int delay;
	unsigned int pause;
	unsigned long int idle_freq;
} tegra_mpdec_tuners_ins = {
	.startdelay = TEGRA_MPDEC_STARTDELAY,
	.delay = TEGRA_MPDEC_DELAY,
	.pause = TEGRA_MPDEC_PAUSE,
	.idle_freq = TEGRA_MPDEC_IDLE_FREQ,
};

static unsigned int NwNs_Threshold[8] = {19, 30, 19, 11, 19, 11, 0, 11};
static unsigned int TwTs_Threshold[8] = {140, 0, 140, 190, 140, 190, 0, 190};

extern unsigned int get_rq_info(void);
extern unsigned int tegra_getspeed(int);

unsigned int state = TEGRA_MPDEC_IDLE;
bool was_paused = false;

static unsigned long get_rate(int cpu)
{
        unsigned long rate = 0;
        rate = tegra_getspeed(cpu);
        return rate;
}

static int get_slowest_cpu(void)
{
        int i, cpu = 0;
        unsigned long rate, slow_rate = 0;

        for (i = 0; i < CONFIG_NR_CPUS; i++) {

                if (!cpu_online(i))
                        continue;

                rate = get_rate(i);

                if (slow_rate == 0) {
                        slow_rate = rate;
                }

                if ((rate <= slow_rate) && (slow_rate != 0)) {
                        if (i == 0)
                                continue;

                        cpu = i;
                        slow_rate = rate;
                }
        }

        return cpu;
}

static int get_slowest_cpu_rate(void)
{
        int i = 0;
        unsigned long rate, slow_rate = 0;

        for (i = 0; i < CONFIG_NR_CPUS; i++) {
                rate = get_rate(i);
                if ((rate < slow_rate) && (slow_rate != 0)) {
                        slow_rate = rate;
                }
                if (slow_rate == 0) {
                        slow_rate = rate;
                }
        }

        return slow_rate;
}

static int mp_decision(void)
{
	static bool first_call = true;
	int new_state = TEGRA_MPDEC_IDLE;
	int nr_cpu_online;
	int index;
	unsigned int rq_depth;
	static cputime64_t total_time = 0;
	static cputime64_t last_time;
	cputime64_t current_time;
	cputime64_t this_time = 0;

	if (state == TEGRA_MPDEC_DISABLED)
		return TEGRA_MPDEC_DISABLED;

	current_time = ktime_to_ms(ktime_get());
	if (current_time <= tegra_mpdec_tuners_ins.startdelay)
		return TEGRA_MPDEC_IDLE;

	if (first_call) {
		first_call = false;
	} else {
		this_time = current_time - last_time;
	}
	total_time += this_time;

	rq_depth = get_rq_info();
	nr_cpu_online = num_online_cpus();

	if (nr_cpu_online) {
		index = (nr_cpu_online - 1) * 2;
		if ((nr_cpu_online < CONFIG_NR_CPUS) && (rq_depth >= NwNs_Threshold[index])) {
			if (total_time >= TwTs_Threshold[index]) {
				new_state = TEGRA_MPDEC_UP;
                                if (get_slowest_cpu_rate() <= tegra_mpdec_tuners_ins.idle_freq)
                                        new_state = TEGRA_MPDEC_IDLE;
			}
		} else if ((nr_cpu_online > 1) && (rq_depth <= NwNs_Threshold[index+1])) {
			if (total_time >= TwTs_Threshold[index+1] ) {
				new_state = TEGRA_MPDEC_DOWN;
		                if (get_slowest_cpu_rate() > tegra_mpdec_tuners_ins.idle_freq)
                                        new_state = TEGRA_MPDEC_IDLE;
			}
		} else {
			new_state = TEGRA_MPDEC_IDLE;
			total_time = 0;
		}
	} else {
		total_time = 0;
	}

	if (new_state != TEGRA_MPDEC_IDLE) {
		total_time = 0;
	}

	last_time = ktime_to_ms(ktime_get());

#if DEBUG
        pr_info(MPDEC_TAG"[DEBUG] rq: %u, new_state: %i | Mask=[%d%d%d%d]\n",
		         rq_depth, new_state, cpu_online(0), cpu_online(1), cpu_online(2), cpu_online(3));
#endif
	return new_state;
}

static void tegra_mpdec_work_thread(struct work_struct *work)
{
	unsigned int cpu = nr_cpu_ids;
	cputime64_t on_time = 0;
        bool suspended = false;

	if (ktime_to_ms(ktime_get()) <= tegra_mpdec_tuners_ins.startdelay)
		goto out;

        for_each_possible_cpu(cpu)
	        if ((per_cpu(tegra_mpdec_cpudata, cpu).device_suspended == true))
                        suspended = true;

	if (suspended == true)
		goto out;

	if (!mutex_trylock(&tegra_cpu_lock))
		goto out;

	/* if sth messed with the cpus, update the check vars so we can proceed */
	if (was_paused) {
		for_each_possible_cpu(cpu) {
			if (cpu_online(cpu))
				per_cpu(tegra_mpdec_cpudata, cpu).online = true;
			else if (!cpu_online(cpu))
				per_cpu(tegra_mpdec_cpudata, cpu).online = false;
		}
		was_paused = false;
	}

	state = mp_decision();
	switch (state) {
	case TEGRA_MPDEC_DISABLED:
	case TEGRA_MPDEC_IDLE:
		break;
	case TEGRA_MPDEC_DOWN:
		cpu = get_slowest_cpu();
		if (cpu < nr_cpu_ids) {
			if ((per_cpu(tegra_mpdec_cpudata, cpu).online == true) && (cpu_online(cpu))) {
				cpu_down(cpu);
				per_cpu(tegra_mpdec_cpudata, cpu).online = false;
				on_time = ktime_to_ms(ktime_get()) - per_cpu(tegra_mpdec_cpudata, cpu).on_time;
				pr_info(MPDEC_TAG"CPU[%d] on->off | Mask=[%d%d%d%d] | time online: %llu\n",
						cpu, cpu_online(0), cpu_online(1), cpu_online(2), cpu_online(3), on_time);
			} else if (per_cpu(tegra_mpdec_cpudata, cpu).online != cpu_online(cpu)) {
				pr_info(MPDEC_TAG"CPU[%d] was controlled outside of mpdecision! | pausing [%d]ms\n",
						cpu, tegra_mpdec_tuners_ins.pause);
				msleep(tegra_mpdec_tuners_ins.pause);
				was_paused = true;
			}
		}
		break;
	case TEGRA_MPDEC_UP:
		cpu = cpumask_next_zero(0, cpu_online_mask);
		if (cpu < nr_cpu_ids) {
			if ((per_cpu(tegra_mpdec_cpudata, cpu).online == false) && (!cpu_online(cpu))) {
				cpu_up(cpu);
				per_cpu(tegra_mpdec_cpudata, cpu).online = true;
				per_cpu(tegra_mpdec_cpudata, cpu).on_time = ktime_to_ms(ktime_get());
				pr_info(MPDEC_TAG"CPU[%d] off->on | Mask=[%d%d%d%d]\n",
						cpu, cpu_online(0), cpu_online(1), cpu_online(2), cpu_online(3));
			} else if (per_cpu(tegra_mpdec_cpudata, cpu).online != cpu_online(cpu)) {
				pr_info(MPDEC_TAG"CPU[%d] was controlled outside of mpdecision! | pausing [%d]ms\n",
						cpu, tegra_mpdec_tuners_ins.pause);
				msleep(tegra_mpdec_tuners_ins.pause);
				was_paused = true;
			}
		}
		break;
	default:
		pr_err(MPDEC_TAG"%s: invalid mpdec hotplug state %d\n",
		       __func__, state);
	}
	mutex_unlock(&tegra_cpu_lock);

out:
	if (state != TEGRA_MPDEC_DISABLED)
		schedule_delayed_work(&tegra_mpdec_work,
				msecs_to_jiffies(tegra_mpdec_tuners_ins.delay));
	return;
}

static void tegra_mpdec_early_suspend(struct early_suspend *h)
{
/* TODO: power down cpu0 and power up lp */
	int cpu = nr_cpu_ids;
	for_each_possible_cpu(cpu) {
		mutex_lock(&per_cpu(tegra_mpdec_cpudata, cpu).suspend_mutex);
		if ((cpu >= 1) && (cpu_online(cpu))) {
                        cpu_down(cpu);
                        pr_info(MPDEC_TAG"Screen -> off. Suspended CPU[%d] | Mask=[%d%d%d%d]\n",
                                        cpu, cpu_online(0), cpu_online(1), cpu_online(2), cpu_online(3));
			per_cpu(tegra_mpdec_cpudata, cpu).online = false;
		}
		per_cpu(tegra_mpdec_cpudata, cpu).device_suspended = true;
		mutex_unlock(&per_cpu(tegra_mpdec_cpudata, cpu).suspend_mutex);
	}
	pr_info(MPDEC_TAG"Screen -> off. Deactivated mpdecision.\n");
}

static void tegra_mpdec_late_resume(struct early_suspend *h)
{
/* TODO:
   power down lp cpu and power up one hp cpu
   afterwards let mpdecision decide if the hp is needed and revert to lp if it is not */
	int cpu = nr_cpu_ids;
	for_each_possible_cpu(cpu) {
		mutex_lock(&per_cpu(tegra_mpdec_cpudata, cpu).suspend_mutex);
		per_cpu(tegra_mpdec_cpudata, cpu).device_suspended = false;
		mutex_unlock(&per_cpu(tegra_mpdec_cpudata, cpu).suspend_mutex);
	}
	pr_info(MPDEC_TAG"Screen -> on. Activated mpdecision. | Mask=[%d%d%d%d]\n",
			cpu_online(0), cpu_online(1), cpu_online(2), cpu_online(3));
}

static struct early_suspend tegra_mpdec_early_suspend_handler = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
	.suspend = tegra_mpdec_early_suspend,
	.resume = tegra_mpdec_late_resume,
};

/**************************** SYSFS START ****************************/
struct kobject *tegra_mpdec_kobject;

#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)               \
{									\
	return sprintf(buf, "%u\n", tegra_mpdec_tuners_ins.object);	\
}

show_one(startdelay, startdelay);
show_one(delay, delay);
show_one(pause, pause);

static ssize_t show_idle_freq (struct kobject *kobj, struct attribute *attr,
                                   char *buf)
{
	return sprintf(buf, "%lu\n", tegra_mpdec_tuners_ins.idle_freq);
}

static ssize_t show_enabled(struct kobject *a, struct attribute *b,
				   char *buf)
{
	unsigned int enabled;
	switch (state) {
	case TEGRA_MPDEC_DISABLED:
		enabled = 0;
		break;
	case TEGRA_MPDEC_IDLE:
	case TEGRA_MPDEC_DOWN:
	case TEGRA_MPDEC_UP:
		enabled = 1;
		break;
	default:
		enabled = 333;
	}
	return sprintf(buf, "%u\n", enabled);
}

static ssize_t show_nwns_threshold_up(struct kobject *kobj, struct attribute *attr,
					char *buf)
{
	return sprintf(buf, "%u\n", NwNs_Threshold[0]);
}

static ssize_t show_nwns_threshold_down(struct kobject *kobj, struct attribute *attr,
					char *buf)
{
	return sprintf(buf, "%u\n", NwNs_Threshold[3]);
}

static ssize_t show_twts_threshold_up(struct kobject *kobj, struct attribute *attr,
					char *buf)
{
	return sprintf(buf, "%u\n", TwTs_Threshold[0]);
}

static ssize_t show_twts_threshold_down(struct kobject *kobj, struct attribute *attr,
					char *buf)
{
	return sprintf(buf, "%u\n", TwTs_Threshold[3]);
}

static ssize_t store_startdelay(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	tegra_mpdec_tuners_ins.startdelay = input;

	return count;
}

static ssize_t store_delay(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	tegra_mpdec_tuners_ins.delay = input;

	return count;
}

static ssize_t store_pause(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	tegra_mpdec_tuners_ins.pause = input;

	return count;
}

static ssize_t store_idle_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	long unsigned int input;
	int ret;
	ret = sscanf(buf, "%lu", &input);
	if (ret != 1)
		return -EINVAL;

	tegra_mpdec_tuners_ins.idle_freq = input;

	return count;
}

static ssize_t store_enabled(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int cpu, input, enabled;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	switch (state) {
	case TEGRA_MPDEC_DISABLED:
		enabled = 0;
		break;
	case TEGRA_MPDEC_IDLE:
	case TEGRA_MPDEC_DOWN:
	case TEGRA_MPDEC_UP:
		enabled = 1;
		break;
	default:
		enabled = 333;
	}

	if (buf[0] == enabled)
		return -EINVAL;

	switch (buf[0]) {
	case '0':
		state = TEGRA_MPDEC_DISABLED;
		cpu = (CONFIG_NR_CPUS - 1);
		if (!cpu_online(cpu)) {
			per_cpu(tegra_mpdec_cpudata, cpu).on_time = ktime_to_ms(ktime_get());
			per_cpu(tegra_mpdec_cpudata, cpu).online = true;
			cpu_up(cpu);
			pr_info(MPDEC_TAG"nap time... Hot plugged CPU[%d] | Mask=[%d%d]\n",
					 cpu, cpu_online(0), cpu_online(1));
		} else {
			pr_info(MPDEC_TAG"nap time...\n");
		}
		break;
	case '1':
		state = TEGRA_MPDEC_IDLE;
		was_paused = true;
		schedule_delayed_work(&tegra_mpdec_work, 0);
		pr_info(MPDEC_TAG"firing up mpdecision...\n");
		break;
	default:
		ret = -EINVAL;
	}
	return count;
}

static ssize_t store_nwns_threshold_up(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	NwNs_Threshold[0] = input;

	return count;
}

static ssize_t store_nwns_threshold_down(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	NwNs_Threshold[3] = input;

	return count;
}

static ssize_t store_twts_threshold_up(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	TwTs_Threshold[0] = input;

	return count;
}

static ssize_t store_twts_threshold_down(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	TwTs_Threshold[3] = input;

	return count;
}

define_one_global_rw(startdelay);
define_one_global_rw(delay);
define_one_global_rw(pause);
define_one_global_rw(idle_freq);
define_one_global_rw(enabled);
define_one_global_rw(nwns_threshold_up);
define_one_global_rw(nwns_threshold_down);
define_one_global_rw(twts_threshold_up);
define_one_global_rw(twts_threshold_down);

static struct attribute *tegra_mpdec_attributes[] = {
	&startdelay.attr,
	&delay.attr,
	&pause.attr,
	&idle_freq.attr,
	&enabled.attr,
	&nwns_threshold_up.attr,
	&nwns_threshold_down.attr,
	&twts_threshold_up.attr,
	&twts_threshold_down.attr,
	NULL
};


static struct attribute_group tegra_mpdec_attr_group = {
	.attrs = tegra_mpdec_attributes,
	.name = "conf",
};
/**************************** SYSFS END ****************************/

static int __init tegra_mpdec(void)
{
	int cpu, rc, err = 0;

	for_each_possible_cpu(cpu) {
		mutex_init(&(per_cpu(tegra_mpdec_cpudata, cpu).suspend_mutex));
		per_cpu(tegra_mpdec_cpudata, cpu).device_suspended = false;
		per_cpu(tegra_mpdec_cpudata, cpu).online = true;
	}

        was_paused = true;

	INIT_DELAYED_WORK(&tegra_mpdec_work, tegra_mpdec_work_thread);
	if (state != TEGRA_MPDEC_DISABLED)
		schedule_delayed_work(&tegra_mpdec_work, 0);

	register_early_suspend(&tegra_mpdec_early_suspend_handler);

	tegra_mpdec_kobject = kobject_create_and_add("tegra_mpdecision", kernel_kobj);
	if (tegra_mpdec_kobject) {
		rc = sysfs_create_group(tegra_mpdec_kobject,
							&tegra_mpdec_attr_group);
		if (rc) {
			pr_warn(MPDEC_TAG"sysfs: ERROR, could not create sysfs group");
		}
	} else
		pr_warn(MPDEC_TAG"sysfs: ERROR, could not create sysfs kobj");

	pr_info(MPDEC_TAG"%s init complete.", __func__);

	return err;
}

late_initcall(tegra_mpdec);


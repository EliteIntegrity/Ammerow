/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file z-rand.c
 * \brief A Random Number Generator for Angband
 *
 * Copyright (c) 1997 Ben Harrison, Randy Hutson
 * 
 * See below for the public-domain notice on the random number generator.
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */
#include "z-rand.h"
#ifdef _WIN32
#include <windows.h> /* GetCurrentProcessId() */
#endif

/**
 * This file provides a pseudo-random number generator.
 *
 * The gameplay stream, deterministic content-generation stream, and cosmetic
 * stream are separate.  Random presentation work therefore cannot perturb
 * gameplay, while seeded generation can be repeated without exposing or
 * replacing the live gameplay state.
 */

/* xoshiro128** 1.1 was written in 2018 by David Blackman and Sebastiano
 * Vigna.  SplitMix64 was written in 2015 by Sebastiano Vigna.  This adapted
 * implementation retains their published permission notice:
 *
 * To the extent possible under law, the author has dedicated all copyright
 * and related and neighboring rights to this software to the public domain
 * worldwide.  Permission to use, copy, modify, and/or distribute this
 * software for any purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR
 * IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Canonical sources and the verbatim notices are recorded in
 * third_party/licenses/Xoshiro-SplitMix-public-domain.txt.
 */
struct random_engine {
	uint32_t words[RAND_STATE_WORDS];
};

static struct random_engine gameplay_stream;
static struct random_engine deterministic_stream;
static struct random_engine cosmetic_stream;
static struct random_engine *active_stream = &gameplay_stream;
static bool gameplay_seeded;
static bool deterministic_active;
static bool cosmetic_seeded;

static uint32_t rotate_left(uint32_t value, int distance)
{
	return (value << distance) | (value >> (32 - distance));
}

static uint32_t random_engine_next(struct random_engine *engine)
{
	uint32_t result = rotate_left(engine->words[1] * 5U, 7) * 9U;
	uint32_t transfer = engine->words[1] << 9;

	engine->words[2] ^= engine->words[0];
	engine->words[3] ^= engine->words[1];
	engine->words[1] ^= engine->words[2];
	engine->words[0] ^= engine->words[3];
	engine->words[2] ^= transfer;
	engine->words[3] = rotate_left(engine->words[3], 11);

	return result;
}

static uint64_t splitmix64_next(uint64_t *state)
{
	uint64_t value = (*state += UINT64_C(0x9e3779b97f4a7c15));

	value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
	value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
	return value ^ (value >> 31);
}

static bool random_engine_is_zero(const struct random_engine *engine)
{
	return !(engine->words[0] | engine->words[1] |
		engine->words[2] | engine->words[3]);
}

static void random_engine_seed(struct random_engine *engine, uint32_t seed)
{
	uint64_t state = seed;
	uint64_t value;

	value = splitmix64_next(&state);
	engine->words[0] = (uint32_t)value;
	engine->words[1] = (uint32_t)(value >> 32);
	value = splitmix64_next(&state);
	engine->words[2] = (uint32_t)value;
	engine->words[3] = (uint32_t)(value >> 32);

	/* xoshiro's only invalid state is four zero words. */
	if (random_engine_is_zero(engine)) engine->words[0] = 1;
}

static uint32_t random_engine_div(struct random_engine *engine, uint32_t m)
{
	uint32_t threshold, value;

	if (m <= 1) return 0;

	/* Reject the short prefix so every residue has exactly the same weight. */
	threshold = (UINT32_MAX - m + 1U) % m;
	do {
		value = random_engine_next(engine);
	} while (value < threshold);

	return value % m;
}

static bool rand_fixed;
static uint32_t rand_fixval;

/**
 * Initialize the complex RNG using a new seed.
 */
void Rand_state_init(uint32_t seed)
{
	assert(!deterministic_active);
	random_engine_seed(&gameplay_stream, seed);
	gameplay_seeded = true;
	active_stream = &gameplay_stream;
}

/**
 * Initialise the RNG
 */
void Rand_init(void)
{
	if (!gameplay_seeded) {
		uint32_t seed;

		/* Basic seed */
		seed = (uint32_t)(time(NULL));

#ifdef UNIX
		/* Mutate the seed on Unix machines */
		seed = ((seed >> 3) * (getpid() << 1));
#elif defined(_WIN32)
		/* Or on Windows */
		seed = ((seed >> 3) * (GetCurrentProcessId() << 1));
#endif

		Rand_state_init(seed);
	}
}

void Rand_state_export(struct randomizer_state *dest)
{
	assert(dest);
	assert(gameplay_seeded);
	dest->algorithm = RAND_ALGORITHM_XOSHIRO128SS;
	memcpy(dest->words, gameplay_stream.words, sizeof(dest->words));
}

bool Rand_state_import(const struct randomizer_state *source)
{
	struct random_engine candidate;

	if (!source || source->algorithm != RAND_ALGORITHM_XOSHIRO128SS)
		return false;
	memcpy(candidate.words, source->words, sizeof(candidate.words));
	if (random_engine_is_zero(&candidate)) return false;

	gameplay_stream = candidate;
	gameplay_seeded = true;
	deterministic_active = false;
	active_stream = &gameplay_stream;
	return true;
}

void Rand_begin_deterministic(uint32_t seed)
{
	assert(!deterministic_active);
	if (deterministic_active) return;

	random_engine_seed(&deterministic_stream, seed);
	deterministic_active = true;
	active_stream = &deterministic_stream;
}

void Rand_end_deterministic(void)
{
	assert(deterministic_active);
	if (!deterministic_active) return;

	deterministic_active = false;
	active_stream = &gameplay_stream;
}


/**
 * Extract a uniformly distributed random number from 0 to m - 1.  Rejection
 * sampling removes the modulo bias when m does not divide the 32-bit range.
 */
uint32_t Rand_div(uint32_t m)
{
	if (m <= 1) return 0;

	if (rand_fixed)
		return (uint32_t)(((uint64_t)rand_fixval * (m - 1)) / 100);

	assert(active_stream);
	return random_engine_div(active_stream, m);
}


/**
 * The number of entries in the "Rand_normal_table"
 */
#define RANDNOR_NUM	256

/**
 * The standard deviation of the "Rand_normal_table"
 */
#define RANDNOR_STD	64

/**
 * The normal distribution table for the "Rand_normal()" function (below)
 */
static int16_t Rand_normal_table[RANDNOR_NUM] = {
	206,   613,   1022,  1430,  1838,  2245,  2652,  3058,
	3463,  3867,  4271,  4673,  5075,  5475,  5874,  6271,
	6667,  7061,  7454,  7845,  8234,  8621,  9006,  9389,
	9770,  10148, 10524, 10898, 11269,	11638,	12004,	12367,
	12727, 13085, 13440, 13792, 14140,	14486,	14828,	15168,
	15504, 15836, 16166, 16492, 16814,	17133,	17449,	17761,
	18069, 18374, 18675, 18972, 19266,	19556,	19842,	20124,
	20403, 20678, 20949, 21216, 21479,	21738,	21994,	22245,

	22493, 22737, 22977, 23213, 23446,	23674,	23899,	24120,
	24336, 24550, 24759, 24965, 25166,	25365,	25559,	25750,
	25937, 26120, 26300, 26476, 26649,	26818,	26983,	27146,
	27304, 27460, 27612, 27760, 27906,	28048,	28187,	28323,
	28455, 28585, 28711, 28835, 28955,	29073,	29188,	29299,
	29409, 29515, 29619, 29720, 29818,	29914,	30007,	30098,
	30186, 30272, 30356, 30437, 30516,	30593,	30668,	30740,
	30810, 30879, 30945, 31010, 31072,	31133,	31192,	31249,

	31304, 31358, 31410, 31460, 31509,	31556,	31601,	31646,
	31688, 31730, 31770, 31808, 31846,	31882,	31917,	31950,
	31983, 32014, 32044, 32074, 32102,	32129,	32155,	32180,
	32205, 32228, 32251, 32273, 32294,	32314,	32333,	32352,
	32370, 32387, 32404, 32420, 32435,	32450,	32464,	32477,
	32490, 32503, 32515, 32526, 32537,	32548,	32558,	32568,
	32577, 32586, 32595, 32603, 32611,	32618,	32625,	32632,
	32639, 32645, 32651, 32657, 32662,	32667,	32672,	32677,

	32682, 32686, 32690, 32694, 32698,	32702,	32705,	32708,
	32711, 32714, 32717, 32720, 32722,	32725,	32727,	32729,
	32731, 32733, 32735, 32737, 32739,	32740,	32742,	32743,
	32745, 32746, 32747, 32748, 32749,	32750,	32751,	32752,
	32753, 32754, 32755, 32756, 32757,	32757,	32758,	32758,
	32759, 32760, 32760, 32761, 32761,	32761,	32762,	32762,
	32763, 32763, 32763, 32764, 32764,	32764,	32764,	32765,
	32765, 32765, 32765, 32766, 32766, 32766, 32766, 32767,
};


/**
 * Generate a random integer number of NORMAL distribution
 *
 * The table above is used to generate a psuedo-normal distribution, in a
 * manner which is much faster than calling a transcendental function to
 * calculate a true normal distribution.
 *
 * Basically, entry 64 * N in the table above represents the number of times
 * out of 32767 that a random variable with normal distribution will fall
 * within N standard deviations of the mean.  That is, about 68 percent of the
 * time for N=1 and 95 percent of the time for N=2.
 *
 * The table above contains a "faked" final entry which allows us to pretend
 * that all values in a normal distribution are strictly less than four
 * standard deviations away from the mean.  This results in "conservative"
 * distribution of approximately 1/32768 values.
 *
 * Note that the binary search takes up to 16 quick iterations.
 */
int16_t Rand_normal(int mean, int stand)
{
	int16_t tmp, offset;

	int16_t low = 0;
	int16_t high = RANDNOR_NUM;

	/* Paranoia */
	if (stand < 1) return (mean);

	/* Roll for probability */
	tmp = (int16_t)randint0(32768);

	/* Binary Search */
	while (low < high) {
		int mid = (low + high) >> 1;

		/* Move right if forced */
		if (Rand_normal_table[mid] < tmp) {
			low = mid + 1;
		} else {
			high = mid;
		}
	}

	/* Convert the index into an offset */
	offset = (int16_t)((long)stand * (long)low / RANDNOR_STD);

	/* One half should be negative */
	if (one_in_(2)) return (mean - offset);

	/* One half should be positive */
	return (mean + offset);
}


/**
 * Choose an integer from a distribution where we know the mean and approximate
 * upper and lower bounds.
 *
 * We divide the imagined distribution into two halves, above and below the
 * mean, and then treat the bounds as if they are the given number of
 * standard deviations from the mean in the appropriate direction.  Note that
 * `stand_u` and `stand_l` are 10 times the number of standart deviations we
 * are asking for.
 * The function chooses an integer from a normal distribution, and then scales
 * it to fit the target distribution.
 */
int Rand_sample(int mean, int upper, int lower, int stand_u, int stand_l)
{
	int pick = Rand_normal(0, 1000);

	/* Scale to fit */
	if (pick > 0) {
		/* Positive pick, scale up */
		pick *= (upper - mean);
		pick /= (100 * stand_u);
	} else if (pick < 0) {
		/* Negative pick, scale down */
		pick *= (mean - lower);
		pick /= (100 * stand_l);
	}

	return mean + pick;
}

/**
 * Generates damage for "2d6" style dice rolls
 */
int damroll(int num, int sides)
{
	int i;
	int sum = 0;

	if (sides <= 0) return 0;

	for (i = 0; i < num; i++)
		sum += randint1(sides);
	return sum;
}



/**
 * Calculation helper function for damroll
 */
int damcalc(int num, int sides, aspect dam_aspect)
{
	switch (dam_aspect) {
		case MAXIMISE:
		case EXTREMIFY: return num * sides;
		case RANDOMISE: return damroll(num, sides);
		case MINIMISE: return num;
		case AVERAGE: return num * (sides + 1) / 2;
	}

	assert(0 && "Should never reach here");
	return 0;
}


/**
 * Generates a random signed long integer X where `A` <= X <= `B`.
 * The integer X falls along a uniform distribution.
 *
 * Note that "rand_range(0, N-1)" == "randint0(N)".
 */
int rand_range(int A, int B)
{
	if (A == B) return A;
	assert(A < B);

	return A + (int32_t)Rand_div(1 + B - A);
}


/**
 * Perform division, possibly rounding up or down depending on the size of the
 * remainder and chance.
 */
static int simulate_division(int dividend, int divisor)
{
	int quotient  = dividend / divisor;
	int remainder = dividend % divisor;
	if (randint0(divisor) < remainder) quotient++;
	return quotient;
}


/**
 * Help determine an "enchantment bonus" for an object.
 *
 * To avoid floating point but still provide a smooth distribution of bonuses,
 * we simply round the results of division in such a way as to "average" the
 * correct floating point value.
 *
 * This function has been changed.  It uses "Rand_normal()" to choose values
 * from a normal distribution, whose mean moves from zero towards the max as
 * the level increases, and whose standard deviation is equal to 1/4 of the
 * max, and whose values are forced to lie between zero and the max, inclusive.
 *
 * Since the "level" rarely passes 100 before the completed-run boss is dead,
 * it is rare to get the "full" enchantment on an object, even at deep levels.
 *
 * It is always possible (albeit unlikely) to get the "full" enchantment.
 *
 * A sample distribution of values from "m_bonus(10, N)" is shown below:
 *
 *   N       0     1     2     3     4     5     6     7     8     9    10
 * ---    ----  ----  ----  ----  ----  ----  ----  ----  ----  ----  ----
 *   0   66.37 13.01  9.73  5.47  2.89  1.31  0.72  0.26  0.12  0.09  0.03
 *   8   46.85 24.66 12.13  8.13  4.20  2.30  1.05  0.36  0.19  0.08  0.05
 *  16   30.12 27.62 18.52 10.52  6.34  3.52  1.95  0.90  0.31  0.15  0.05
 *  24   22.44 15.62 30.14 12.92  8.55  5.30  2.39  1.63  0.62  0.28  0.11
 *  32   16.23 11.43 23.01 22.31 11.19  7.18  4.46  2.13  1.20  0.45  0.41
 *  40   10.76  8.91 12.80 29.51 16.00  9.69  5.90  3.43  1.47  0.88  0.65
 *  48    7.28  6.81 10.51 18.27 27.57 11.76  7.85  4.99  2.80  1.22  0.94
 *  56    4.41  4.73  8.52 11.96 24.94 19.78 11.06  7.18  3.68  1.96  1.78
 *  64    2.81  3.07  5.65  9.17 13.01 31.57 13.70  9.30  6.04  3.04  2.64
 *  72    1.87  1.99  3.68  7.15 10.56 20.24 25.78 12.17  7.52  4.42  4.62
 *  80    1.02  1.23  2.78  4.75  8.37 12.04 27.61 18.07 10.28  6.52  7.33
 *  88    0.70  0.57  1.56  3.12  6.34 10.06 15.76 30.46 12.58  8.47 10.38
 *  96    0.27  0.60  1.25  2.28  4.30  7.60 10.77 22.52 22.51 11.37 16.53
 * 104    0.22  0.42  0.77  1.36  2.62  5.33  8.93 13.05 29.54 15.23 22.53
 * 112    0.15  0.20  0.56  0.87  2.00  3.83  6.86 10.06 17.89 27.31 30.27
 * 120    0.03  0.11  0.31  0.46  1.31  2.48  4.60  7.78 11.67 25.53 45.72
 * 128    0.02  0.01  0.13  0.33  0.83  1.41  3.24  6.17  9.57 14.22 64.07
 */
int16_t m_bonus(int max, int level)
{
	int bonus, stand, value;

	/* Make sure level is reasonable */
	if (level >= MAX_RAND_DEPTH) level = MAX_RAND_DEPTH - 1;

	/* The bonus approaches max as level approaches MAX_RAND_DEPTH */
	bonus = simulate_division(max * level, MAX_RAND_DEPTH);

	/* The standard deviation is 1/4 of the max */
	stand = simulate_division(max, 4);

	/* Choose a value */
	value = Rand_normal(bonus, stand);

	/* Return, enforcing the min and max values */
	if (value < 0)
		return 0;
	else if (value > max)
		return max;
	else
		return value;
}


/**
 * Calculation helper function for m_bonus
 */
int16_t m_bonus_calc(int max, int level, aspect bonus_aspect)
{
	switch (bonus_aspect) {
		case EXTREMIFY:
		case MAXIMISE:  return max;
		case RANDOMISE: return m_bonus(max, level);
		case MINIMISE:  return 0;
		case AVERAGE:   return max * level / MAX_RAND_DEPTH;
	}

	assert(0 && "Should never reach here");
	return 0;
}


/**
 * Calculation helper function for random_value structs
 */
int randcalc(random_value v, int level, aspect rand_aspect)
{
	if (rand_aspect == EXTREMIFY) {
		int min = randcalc(v, level, MINIMISE);
		int max = randcalc(v, level, MAXIMISE);
		return abs(min) > abs(max) ? min : max;

	} else {
		int dmg   = damcalc(v.dice, v.sides, rand_aspect);
		int bonus = m_bonus_calc(v.m_bonus, level, rand_aspect);
		return v.base + dmg + bonus;
	}
}


/**
 * Test to see if a value is within a random_value's range
 */
bool randcalc_valid(random_value v, int test)
{
	if (test < randcalc(v, 0, MINIMISE))
		return false;
	else if (test > randcalc(v, 0, MAXIMISE))
		return false;
	else
		return true;
}

/**
 * Test to see if a random_value actually varies
 */
bool randcalc_varies(random_value v)
{
	return randcalc(v, 0, MINIMISE) != randcalc(v, 0, MAXIMISE);
}

/**
 * Roll on a random chance and check for success.
 *
 * \param c The random_chance to roll on
 */
bool random_chance_check(random_chance c)
{
	/* Calculated so that high rolls pass the check */
	return randint0(c.denominator) >= c.denominator - c.numerator;
}

/**
 * Scales a random chance to use the denominator provided in the scale argument
 * and returns the appropriate numerator. For example, a chance of 7 / 13 (53.8%)
 * with scale 100 would be 53. For extra integer precision, a scale of 1000 would
 * yield 538.
 *
 * \param c The random_chance to scale
 * \param scale The scale by which the ratio is multiplied
 */
int random_chance_scaled(random_chance c, int scale)
{
	return scale * c.numerator / c.denominator;
}

/**
 * Cause the output from Rand_div() to be fixed rather than random.
 *
 * \param val Is the percent of the maximum value that Rand_div() will
 * return.  val should be between 0 and 100, inclusive.
 */
void rand_fix(uint32_t val)
{
	assert(val <= 100);
	rand_fixed = true;
	rand_fixval = val;
}

/**
 * Restore ordinary random output after rand_fix().
 */
void rand_unfix(void)
{
	rand_fixed = false;
	rand_fixval = 0;
}

/**
 * Another simple RNG that does not use any of the above state
 * (so can be used without disturbing the game's RNG state)
 */
uint32_t Rand_simple(uint32_t m)
{
	if (!cosmetic_seeded) {
		uint32_t seed = (uint32_t)time(NULL);

#ifdef UNIX
		seed ^= (uint32_t)getpid() * UINT32_C(0x9e3779b9);
#elif defined(_WIN32)
		seed ^= (uint32_t)GetCurrentProcessId() * UINT32_C(0x9e3779b9);
#endif
		random_engine_seed(&cosmetic_stream, seed ^ UINT32_C(0xc05e71c5));
		cosmetic_seeded = true;
	}

	return random_engine_div(&cosmetic_stream, m);
}

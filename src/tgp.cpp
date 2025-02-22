/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tgp.cpp OTTD Perlin Noise Landscape Generator, aka TerraGenesis Perlin */

#include "stdafx.h"
#include <math.h>
#include "clear_map.h"
#include "void_map.h"
#include "genworld.h"
#include "core/alloc_func.hpp"
#include "core/random_func.hpp"
#include "landscape_type.h"

/*
 *
 * Quickie guide to Perlin Noise
 * Perlin noise is a predictable pseudo random number sequence. By generating
 * it in 2 dimensions, it becomes a useful random map that, for a given seed
 * and starting X & Y, is entirely predictable. On the face of it, that may not
 * be useful. However, it means that if you want to replay a map in a different
 * terrain, or just vary the sea level, you just re-run the generator with the
 * same seed. The seed is an int32, and is randomised on each run of New Game.
 * The Scenario Generator does not randomise the value, so that you can
 * experiment with one terrain until you are happy, or click "Random" for a new
 * random seed.
 *
 * Perlin Noise is a series of "octaves" of random noise added together. By
 * reducing the amplitude of the noise with each octave, the first octave of
 * noise defines the main terrain sweep, the next the ripples on that, and the
 * next the ripples on that. I use 6 octaves, with the amplitude controlled by
 * a power ratio, usually known as a persistence or p value. This I vary by the
 * smoothness selection, as can be seen in the table below. The closer to 1,
 * the more of that octave is added. Each octave is however raised to the power
 * of its position in the list, so the last entry in the "smooth" row, 0.35, is
 * raised to the power of 6, so can only add 0.001838...  of the amplitude to
 * the running total.
 *
 * In other words; the first p value sets the general shape of the terrain, the
 * second sets the major variations to that, ... until finally the smallest
 * bumps are added.
 *
 * Usefully, this routine is totally scaleable; so when 32bpp comes along, the
 * terrain can be as bumpy as you like! It is also infinitely expandable; a
 * single random seed terrain continues in X & Y as far as you care to
 * calculate. In theory, we could use just one seed value, but randomly select
 * where in the Perlin XY space we use for the terrain. Personally I prefer
 * using a simple (0, 0) to (X, Y), with a varying seed.
 *
 *
 * Other things i have had to do: mountainous wasnt mountainous enough, and
 * since we only have 0..15 heights available, I add a second generated map
 * (with a modified seed), onto the original. This generally raises the
 * terrain, which then needs scaling back down. Overall effect is a general
 * uplift.
 *
 * However, the values on the top of mountains are then almost guaranteed to go
 * too high, so large flat plateaus appeared at height 15. To counter this, I
 * scale all heights above 12 to proportion up to 15. It still makes the
 * mountains have flatish tops, rather than craggy peaks, but at least they
 * arent smooth as glass.
 *
 *
 * For a full discussion of Perlin Noise, please visit:
 * http://freespace.virgin.net/hugo.elias/models/m_perlin.htm
 *
 *
 * Evolution II
 *
 * The algorithm as described in the above link suggests to compute each tile height
 * as composition of several noise waves. Some of them are computed directly by
 * noise(x, y) function, some are calculated using linear approximation. Our
 * first implementation of perlin_noise_2D() used 4 noise(x, y) calls plus
 * 3 linear interpolations. It was called 6 times for each tile. This was a bit
 * CPU expensive.
 *
 * The following implementation uses optimized algorithm that should produce
 * the same quality result with much less computations, but more memory accesses.
 * The overal speedup should be 300% to 800% depending on CPU and memory speed.
 *
 * I will try to explain it on the example below:
 *
 * Have a map of 4 x 4 tiles, our simplifiead noise generator produces only two
 * values -1 and +1, use 3 octaves with wave lenght 1, 2 and 4, with amplitudes
 * 3, 2, 1. Original algorithm produces:
 *
 * h00 = lerp(lerp(-3, 3, 0/4), lerp(3, -3, 0/4), 0/4) + lerp(lerp(-2,  2, 0/2), lerp( 2, -2, 0/2), 0/2) + -1 = lerp(-3.0,  3.0, 0/4) + lerp(-2,  2, 0/2) + -1 = -3.0  + -2 + -1 = -6.0
 * h01 = lerp(lerp(-3, 3, 1/4), lerp(3, -3, 1/4), 0/4) + lerp(lerp(-2,  2, 1/2), lerp( 2, -2, 1/2), 0/2) +  1 = lerp(-1.5,  1.5, 0/4) + lerp( 0,  0, 0/2) +  1 = -1.5  +  0 +  1 = -0.5
 * h02 = lerp(lerp(-3, 3, 2/4), lerp(3, -3, 2/4), 0/4) + lerp(lerp( 2, -2, 0/2), lerp(-2,  2, 0/2), 0/2) + -1 = lerp(   0,    0, 0/4) + lerp( 2, -2, 0/2) + -1 =    0  +  2 + -1 =  1.0
 * h03 = lerp(lerp(-3, 3, 3/4), lerp(3, -3, 3/4), 0/4) + lerp(lerp( 2, -2, 1/2), lerp(-2,  2, 1/2), 0/2) +  1 = lerp( 1.5, -1.5, 0/4) + lerp( 0,  0, 0/2) +  1 =  1.5  +  0 +  1 =  2.5
 *
 * h10 = lerp(lerp(-3, 3, 0/4), lerp(3, -3, 0/4), 1/4) + lerp(lerp(-2,  2, 0/2), lerp( 2, -2, 0/2), 1/2) +  1 = lerp(-3.0,  3.0, 1/4) + lerp(-2,  2, 1/2) +  1 = -1.5  +  0 +  1 = -0.5
 * h11 = lerp(lerp(-3, 3, 1/4), lerp(3, -3, 1/4), 1/4) + lerp(lerp(-2,  2, 1/2), lerp( 2, -2, 1/2), 1/2) + -1 = lerp(-1.5,  1.5, 1/4) + lerp( 0,  0, 1/2) + -1 = -0.75 +  0 + -1 = -1.75
 * h12 = lerp(lerp(-3, 3, 2/4), lerp(3, -3, 2/4), 1/4) + lerp(lerp( 2, -2, 0/2), lerp(-2,  2, 0/2), 1/2) +  1 = lerp(   0,    0, 1/4) + lerp( 2, -2, 1/2) +  1 =    0  +  0 +  1 =  1.0
 * h13 = lerp(lerp(-3, 3, 3/4), lerp(3, -3, 3/4), 1/4) + lerp(lerp( 2, -2, 1/2), lerp(-2,  2, 1/2), 1/2) + -1 = lerp( 1.5, -1.5, 1/4) + lerp( 0,  0, 1/2) + -1 =  0.75 +  0 + -1 = -0.25
 *
 *
 * Optimization 1:
 *
 * 1) we need to allocate a bit more tiles: (size_x + 1) * (size_y + 1) = (5 * 5):
 *
 * 2) setup corner values using amplitude 3
 * {    -3.0        X          X          X          3.0   }
 * {     X          X          X          X          X     }
 * {     X          X          X          X          X     }
 * {     X          X          X          X          X     }
 * {     3.0        X          X          X         -3.0   }
 *
 * 3a) interpolate values in the middle
 * {    -3.0        X          0.0        X          3.0   }
 * {     X          X          X          X          X     }
 * {     0.0        X          0.0        X          0.0   }
 * {     X          X          X          X          X     }
 * {     3.0        X          0.0        X         -3.0   }
 *
 * 3b) add patches with amplitude 2 to them
 * {    -5.0        X          2.0        X          1.0   }
 * {     X          X          X          X          X     }
 * {     2.0        X         -2.0        X          2.0   }
 * {     X          X          X          X          X     }
 * {     1.0        X          2.0        X         -5.0   }
 *
 * 4a) interpolate values in the middle
 * {    -5.0       -1.5        2.0        1.5        1.0   }
 * {    -1.5       -0.75       0.0        0.75       1.5   }
 * {     2.0        0.0       -2.0        0.0        2.0   }
 * {     1.5        0.75       0.0       -0.75      -1.5   }
 * {     1.0        1.5        2.0       -1.5       -5.0   }
 *
 * 4b) add patches with amplitude 1 to them
 * {    -6.0       -0.5        1.0        2.5        0.0   }
 * {    -0.5       -1.75       1.0       -0.25       2.5   }
 * {     1.0        1.0       -3.0        1.0        1.0   }
 * {     2.5       -0.25       1.0       -1.75      -0.5   }
 * {     0.0        2.5        1.0       -0.5       -6.0   }
 *
 *
 *
 * Optimization 2:
 *
 * As you can see above, each noise function was called just once. Therefore
 * we don't need to use noise function that calculates the noise from x, y and
 * some prime. The same quality result we can obtain using standard Random()
 * function instead.
 *
 */

/** Fixed point type for heights */
typedef int16 height_t;
static const int height_decimal_bits = 4;
static const height_t _invalid_height = -32768;

/** Fixed point array for amplitudes (and percent values) */
typedef int amplitude_t;
static const int amplitude_decimal_bits = 10;

/** Height map - allocated array of heights (MapSizeX() + 1) x (MapSizeY() + 1) */
struct HeightMap
{
	height_t *h;         //< array of heights
	uint     dim_x;      //< height map size_x MapSizeX() + 1
	uint     total_size; //< height map total size
	uint     size_x;     //< MapSizeX()
	uint     size_y;     //< MapSizeY()

	/**
	 * Height map accessor
	 * @param x X position
	 * @param y Y position
	 * @return height as fixed point number
	 */
	inline height_t &height(uint x, uint y)
	{
		return h[x + y * dim_x];
	}
};

/** Global height map instance */
static HeightMap _height_map = {NULL, 0, 0, 0, 0};

/** Conversion: int to height_t */
#define I2H(i) ((i) << height_decimal_bits)
/** Conversion: height_t to int */
#define H2I(i) ((i) >> height_decimal_bits)

/** Conversion: int to amplitude_t */
#define I2A(i) ((i) << amplitude_decimal_bits)
/** Conversion: amplitude_t to int */
#define A2I(i) ((i) >> amplitude_decimal_bits)

/** Conversion: amplitude_t to height_t */
#define A2H(a) ((a) >> (amplitude_decimal_bits - height_decimal_bits))


/** Walk through all items of _height_map.h */
#define FOR_ALL_TILES_IN_HEIGHT(h) for (h = _height_map.h; h < &_height_map.h[_height_map.total_size]; h++)

/** Maximum index into array of noise amplitudes */
static const int TGP_FREQUENCY_MAX = 6;

/**
 * Noise amplitudes (multiplied by 1024)
 * - indexed by "smoothness setting" and log2(frequency)
 */
static const amplitude_t _amplitudes_by_smoothness_and_frequency[4][TGP_FREQUENCY_MAX + 1] = {
	/* lowest frequncy....  ...highest (every corner) */
	/* Very smooth */
	{16000,  5600,  1968,   688,   240,    16,    16},
	/* Smooth */
	{16000, 16000,  6448,  3200,  1024,   128,    16},
	/* Rough */
	{16000, 19200, 12800,  8000,  3200,   256,    64},
	/* Very Rough */
	{24000, 16000, 19200, 16000,  8000,   512,   320},
};

/** Desired water percentage (100% == 1024) - indexed by _settings_game.difficulty.quantity_sea_lakes */
static const amplitude_t _water_percent[4] = {20, 80, 250, 400};

/** Desired maximum height - indexed by _settings_game.difficulty.terrain_type */
static const int8 _max_height[4] = {
	6,       ///< Very flat
	9,       ///< Flat
	12,      ///< Hilly
	15,      ///< Mountainous
};

/**
 * Check if a X/Y set are within the map.
 * @param x coordinate x
 * @param y coordinate y
 * @return true if within the map
 */
static inline bool IsValidXY(uint x, uint y)
{
	return ((int)x) >= 0 && x < _height_map.size_x && ((int)y) >= 0 && y < _height_map.size_y;
}


/**
 * Allocate array of (MapSizeX()+1)*(MapSizeY()+1) heights and init the _height_map structure members
 * @return true on success
 */
static inline bool AllocHeightMap()
{
	height_t *h;

	_height_map.size_x = MapSizeX();
	_height_map.size_y = MapSizeY();

	/* Allocate memory block for height map row pointers */
	_height_map.total_size = (_height_map.size_x + 1) * (_height_map.size_y + 1);
	_height_map.dim_x = _height_map.size_x + 1;
	_height_map.h = CallocT<height_t>(_height_map.total_size);

	/* Iterate through height map initialize values */
	FOR_ALL_TILES_IN_HEIGHT(h) *h = _invalid_height;

	return true;
}

/** Free height map */
static inline void FreeHeightMap()
{
	if (_height_map.h == NULL) return;
	free(_height_map.h);
	_height_map.h = NULL;
}

/**
 * Generates new random height in given amplitude (generated numbers will range from - amplitude to + amplitude)
 * @param rMax Limit of result
 * @return generated height
 */
static inline height_t RandomHeight(amplitude_t rMax)
{
	amplitude_t ra = (Random() << 16) | (Random() & 0x0000FFFF);
	height_t rh;
	/* Spread height into range -rMax..+rMax */
	rh = A2H(ra % (2 * rMax + 1) - rMax);
	return rh;
}

/**
 * One interpolation and noise round
 *
 * The heights on the map are generated in an iterative process.
 * We start off with a frequency of 1 (log_frequency == 0), and generate heights only for corners on the most coarsly mesh
 * (i.e. only for x/y coordinates which are multiples of the minimum edge length).
 *
 * After this initial step the frequency is doubled (log_frequency incremented) each iteration to generate corners on the next finer mesh.
 * The heights of the newly added corners are first set by interpolating the heights from the previous iteration.
 * Finally noise with the given amplitude is applied to all corners of the new mesh.
 *
 * Generation terminates, when the frequency has reached the map size. I.e. the mesh is as fine as the map, and every corner height
 * has been set.
 *
 * @param log_frequency frequency (logarithmic) to apply noise for
 * @param amplitude Amplitude for the noise
 * @return false if we are finished (reached the minimal step size / highest frequency)
 */
static bool ApplyNoise(uint log_frequency, amplitude_t amplitude)
{
	uint size_min = min(_height_map.size_x, _height_map.size_y);
	uint step = size_min >> log_frequency;
	uint x, y;

	/* Trying to apply noise to uninitialized height map */
	assert(_height_map.h != NULL);

	/* Are we finished? */
	if (step == 0) return false;

	if (log_frequency == 0) {
		/* This is first round, we need to establish base heights with step = size_min */
		for (y = 0; y <= _height_map.size_y; y += step) {
			for (x = 0; x <= _height_map.size_x; x += step) {
				height_t height = (amplitude > 0) ? RandomHeight(amplitude) : 0;
				_height_map.height(x, y) = height;
			}
		}
		return true;
	}

	/* It is regular iteration round.
	 * Interpolate height values at odd x, even y tiles */
	for (y = 0; y <= _height_map.size_y; y += 2 * step) {
		for (x = 0; x < _height_map.size_x; x += 2 * step) {
			height_t h00 = _height_map.height(x + 0 * step, y);
			height_t h02 = _height_map.height(x + 2 * step, y);
			height_t h01 = (h00 + h02) / 2;
			_height_map.height(x + 1 * step, y) = h01;
		}
	}

	/* Interpolate height values at odd y tiles */
	for (y = 0; y < _height_map.size_y; y += 2 * step) {
		for (x = 0; x <= _height_map.size_x; x += step) {
			height_t h00 = _height_map.height(x, y + 0 * step);
			height_t h20 = _height_map.height(x, y + 2 * step);
			height_t h10 = (h00 + h20) / 2;
			_height_map.height(x, y + 1 * step) = h10;
		}
	}

	/* Add noise for next higher frequency (smaller steps) */
	for (y = 0; y <= _height_map.size_y; y += step) {
		for (x = 0; x <= _height_map.size_x; x += step) {
			_height_map.height(x, y) += RandomHeight(amplitude);
		}
	}

	return (step > 1);
}

/** Base Perlin noise generator - fills height map with raw Perlin noise */
static void HeightMapGenerate()
{
	uint size_min = min(_height_map.size_x, _height_map.size_y);
	uint iteration_round = 0;
	amplitude_t amplitude;
	bool continue_iteration;
	int log_size_min, log_frequency_min;
	int log_frequency;

	/* Find first power of two that fits, so that later log_frequency == TGP_FREQUENCY_MAX in the last iteration */
	for (log_size_min = TGP_FREQUENCY_MAX; (1U << log_size_min) < size_min; log_size_min++) { }
	log_frequency_min = log_size_min - TGP_FREQUENCY_MAX;

	/* Zero must be part of the iteration, else initialization will fail. */
	assert(log_frequency_min >= 0);

	/* Keep increasing the frequency until we reach the step size equal to one tile */
	do {
		log_frequency = iteration_round - log_frequency_min;
		if (log_frequency >= 0) {
			/* Apply noise for the next frequency */
			assert(log_frequency <= TGP_FREQUENCY_MAX);
			amplitude = _amplitudes_by_smoothness_and_frequency[_settings_game.game_creation.tgen_smoothness][log_frequency];
		} else {
			/* Amplitude for the low frequencies on big maps is 0, i.e. initialise with zero height */
			amplitude = 0;
		}
		continue_iteration = ApplyNoise(iteration_round, amplitude);
		iteration_round++;
	} while (continue_iteration);
	assert(log_frequency == TGP_FREQUENCY_MAX);
}

/** Returns min, max and average height from height map */
static void HeightMapGetMinMaxAvg(height_t *min_ptr, height_t *max_ptr, height_t *avg_ptr)
{
	height_t h_min, h_max, h_avg, *h;
	int64 h_accu = 0;
	h_min = h_max = _height_map.height(0, 0);

	/* Get h_min, h_max and accumulate heights into h_accu */
	FOR_ALL_TILES_IN_HEIGHT(h) {
		if (*h < h_min) h_min = *h;
		if (*h > h_max) h_max = *h;
		h_accu += *h;
	}

	/* Get average height */
	h_avg = (height_t)(h_accu / (_height_map.size_x * _height_map.size_y));

	/* Return required results */
	if (min_ptr != NULL) *min_ptr = h_min;
	if (max_ptr != NULL) *max_ptr = h_max;
	if (avg_ptr != NULL) *avg_ptr = h_avg;
}

/** Dill histogram and return pointer to its base point - to the count of zero heights */
static int *HeightMapMakeHistogram(height_t h_min, height_t h_max, int *hist_buf)
{
	int *hist = hist_buf - h_min;
	height_t *h;

	/* Count the heights and fill the histogram */
	FOR_ALL_TILES_IN_HEIGHT(h) {
		assert(*h >= h_min);
		assert(*h <= h_max);
		hist[*h]++;
	}
	return hist;
}

/** Applies sine wave redistribution onto height map */
static void HeightMapSineTransform(height_t h_min, height_t h_max)
{
	height_t *h;

	FOR_ALL_TILES_IN_HEIGHT(h) {
		double fheight;

		if (*h < h_min) continue;

		/* Transform height into 0..1 space */
		fheight = (double)(*h - h_min) / (double)(h_max - h_min);
		/* Apply sine transform depending on landscape type */
		switch (_settings_game.game_creation.landscape) {
			case LT_TOYLAND:
			case LT_TEMPERATE:
				/* Move and scale 0..1 into -1..+1 */
				fheight = 2 * fheight - 1;
				/* Sine transform */
				fheight = sin(fheight * M_PI_2);
				/* Transform it back from -1..1 into 0..1 space */
				fheight = 0.5 * (fheight + 1);
				break;

			case LT_ARCTIC:
				{
					/* Arctic terrain needs special height distribution.
					 * Redistribute heights to have more tiles at highest (75%..100%) range */
					double sine_upper_limit = 0.75;
					double linear_compression = 2;
					if (fheight >= sine_upper_limit) {
						/* Over the limit we do linear compression up */
						fheight = 1.0 - (1.0 - fheight) / linear_compression;
					} else {
						double m = 1.0 - (1.0 - sine_upper_limit) / linear_compression;
						/* Get 0..sine_upper_limit into -1..1 */
						fheight = 2.0 * fheight / sine_upper_limit - 1.0;
						/* Sine wave transform */
						fheight = sin(fheight * M_PI_2);
						/* Get -1..1 back to 0..(1 - (1 - sine_upper_limit) / linear_compression) == 0.0..m */
						fheight = 0.5 * (fheight + 1.0) * m;
					}
				}
				break;

			case LT_TROPIC:
				{
					/* Desert terrain needs special height distribution.
					 * Half of tiles should be at lowest (0..25%) heights */
					double sine_lower_limit = 0.5;
					double linear_compression = 2;
					if (fheight <= sine_lower_limit) {
						/* Under the limit we do linear compression down */
						fheight = fheight / linear_compression;
					} else {
						double m = sine_lower_limit / linear_compression;
						/* Get sine_lower_limit..1 into -1..1 */
						fheight = 2.0 * ((fheight - sine_lower_limit) / (1.0 - sine_lower_limit)) - 1.0;
						/* Sine wave transform */
						fheight = sin(fheight * M_PI_2);
						/* Get -1..1 back to (sine_lower_limit / linear_compression)..1.0 */
						fheight = 0.5 * ((1.0 - m) * fheight + (1.0 + m));
					}
				}
				break;

			default:
				NOT_REACHED();
				break;
		}
		/* Transform it back into h_min..h_max space */
		*h = (height_t)(fheight * (h_max - h_min) + h_min);
		if (*h < 0) *h = I2H(0);
		if (*h >= h_max) *h = h_max - 1;
	}
}

/* Additional map variety is provided by applying different curve maps
 * to different parts of the map. A randomized low resolution grid contains
 * which curve map to use on each part of the make. This filtered non-linearly
 * to smooth out transitions between curves, so each tile could have between
 * 100% of one map applied or 25% of four maps.
 *
 * The curve maps define different land styles, i.e. lakes, low-lands, hills
 * and mountain ranges, although these are dependent on the landscape style
 * chosen as well.
 *
 * The level parameter dictates the resolution of the grid. A low resolution
 * grid will result in larger continuous areas of a land style, a higher
 * resolution grid splits the style into smaller areas.
 *
 * At this point in map generation, all height data has been normalized to 0
 * to 239.
 */
struct control_point_t {
	height_t x;
	height_t y;
};

struct control_point_list_t {
	size_t length;
	const control_point_t *list;
};

static const control_point_t _curve_map_1[] = {
	{ 0, 0 }, { 48, 24 }, { 192, 32 }, { 240, 96 }
};

static const control_point_t _curve_map_2[] = {
	{ 0, 0 }, { 16, 24 }, { 128, 32 }, { 192, 64 }, { 240, 144 }
};

static const control_point_t _curve_map_3[] = {
	{ 0, 0 }, { 16, 24 }, { 128, 64 }, { 192, 144 }, { 240, 192 }
};

static const control_point_t _curve_map_4[] = {
	{ 0, 0 }, { 16, 24 }, { 96, 72 }, { 160, 192 }, { 220, 239 }, { 240, 239 }
};

static const control_point_list_t _curve_maps[] = {
	{ lengthof(_curve_map_1), _curve_map_1 },
	{ lengthof(_curve_map_2), _curve_map_2 },
	{ lengthof(_curve_map_3), _curve_map_3 },
	{ lengthof(_curve_map_4), _curve_map_4 },
};

static void HeightMapCurves(uint level)
{
	height_t ht[lengthof(_curve_maps)];

	/* Set up a grid to choose curve maps based on location */
	uint sx = Clamp(1 << level, 2, 32);
	uint sy = Clamp(1 << level, 2, 32);
	byte *c = (byte *)alloca(sx * sy);

	for (uint i = 0; i < sx * sy; i++) {
		c[i] = Random() % lengthof(_curve_maps);
	}

	/* Apply curves */
	for (uint x = 0; x < _height_map.size_x; x++) {

		/* Get our X grid positions and bi-linear ratio */
		float fx = (float)(sx * x) / _height_map.size_x + 0.5f;
		uint x1 = (uint)fx;
		uint x2 = x1;
		float xr = 2.0f * (fx - x1) - 1.0f;
		xr = sin(xr * M_PI_2);
		xr = sin(xr * M_PI_2);
		xr = 0.5f * (xr + 1.0f);
		float xri = 1.0f - xr;

		if (x1 > 0) {
			x1--;
			if (x2 >= sx) x2--;
		}

		for (uint y = 0; y < _height_map.size_y; y++) {

			/* Get our Y grid position and bi-linear ratio */
			float fy = (float)(sy * y) / _height_map.size_y + 0.5f;
			uint y1 = (uint)fy;
			uint y2 = y1;
			float yr = 2.0f * (fy - y1) - 1.0f;
			yr = sin(yr * M_PI_2);
			yr = sin(yr * M_PI_2);
			yr = 0.5f * (yr + 1.0f);
			float yri = 1.0f - yr;

			if (y1 > 0) {
				y1--;
				if (y2 >= sy) y2--;
			}

			uint corner_a = c[x1 + sx * y1];
			uint corner_b = c[x1 + sx * y2];
			uint corner_c = c[x2 + sx * y1];
			uint corner_d = c[x2 + sx * y2];

			/* Bitmask of which curve maps are chosen, so that we do not bother
			 * calculating a curve which won't be used. */
			uint corner_bits = 0;
			corner_bits |= 1 << corner_a;
			corner_bits |= 1 << corner_b;
			corner_bits |= 1 << corner_c;
			corner_bits |= 1 << corner_d;

			height_t *h = &_height_map.height(x, y);

			/* Apply all curve maps that are used on this tile. */
			for (uint t = 0; t < lengthof(_curve_maps); t++) {
				if (!HasBit(corner_bits, t)) continue;

				const control_point_t *cm = _curve_maps[t].list;
				for (uint i = 0; i < _curve_maps[t].length - 1; i++) {
					const control_point_t &p1 = cm[i];
					const control_point_t &p2 = cm[i + 1];

					if (*h >= p1.x && *h < p2.x) {
						ht[t] = p1.y + (*h - p1.x) * (p2.y - p1.y) / (p2.x - p1.x);
						break;
					}
				}
			}

			/* Apply interpolation of curve map results. */
			*h = (height_t)((ht[corner_a] * yri + ht[corner_b] * yr) * xri + (ht[corner_c] * yri + ht[corner_d] * yr) * xr);
		}
	}
}

/** Adjusts heights in height map to contain required amount of water tiles */
static void HeightMapAdjustWaterLevel(amplitude_t water_percent, height_t h_max_new)
{
	height_t h_min, h_max, h_avg, h_water_level;
	int64 water_tiles, desired_water_tiles;
	height_t *h;
	int *hist;

	HeightMapGetMinMaxAvg(&h_min, &h_max, &h_avg);

	/* Allocate histogram buffer and clear its cells */
	int *hist_buf = CallocT<int>(h_max - h_min + 1);
	/* Fill histogram */
	hist = HeightMapMakeHistogram(h_min, h_max, hist_buf);

	/* How many water tiles do we want? */
	desired_water_tiles = A2I(((int64)water_percent) * (int64)(_height_map.size_x * _height_map.size_y));

	/* Raise water_level and accumulate values from histogram until we reach required number of water tiles */
	for (h_water_level = h_min, water_tiles = 0; h_water_level < h_max; h_water_level++) {
		water_tiles += hist[h_water_level];
		if (water_tiles >= desired_water_tiles) break;
	}

	/* We now have the proper water level value.
	 * Transform the height map into new (normalized) height map:
	 *   values from range: h_min..h_water_level will become negative so it will be clamped to 0
	 *   values from range: h_water_level..h_max are transformed into 0..h_max_new
	 *   where h_max_new is 4, 8, 12 or 16 depending on terrain type (very flat, flat, hilly, mountains)
	 */
	FOR_ALL_TILES_IN_HEIGHT(h) {
		/* Transform height from range h_water_level..h_max into 0..h_max_new range */
		*h = (height_t)(((int)h_max_new) * (*h - h_water_level) / (h_max - h_water_level)) + I2H(1);
		/* Make sure all values are in the proper range (0..h_max_new) */
		if (*h < 0) *h = I2H(0);
		if (*h >= h_max_new) *h = h_max_new - 1;
	}

	free(hist_buf);
}

static double perlin_coast_noise_2D(const double x, const double y, const double p, const int prime);

/**
 * This routine sculpts in from the edge a random amount, again a Perlin
 * sequence, to avoid the rigid flat-edge slopes that were present before. The
 * Perlin noise map doesnt know where we are going to slice across, and so we
 * often cut straight through high terrain. the smoothing routine makes it
 * legal, gradually increasing up from the edge to the original terrain height.
 * By cutting parts of this away, it gives a far more irregular edge to the
 * map-edge. Sometimes it works beautifully with the existing sea & lakes, and
 * creates a very realistic coastline. Other times the variation is less, and
 * the map-edge shows its cliff-like roots.
 *
 * This routine may be extended to randomly sculpt the height of the terrain
 * near the edge. This will have the coast edge at low level (1-3), rising in
 * smoothed steps inland to about 15 tiles in. This should make it look as
 * though the map has been built for the map size, rather than a slice through
 * a larger map.
 *
 * Please note that all the small numbers; 53, 101, 167, etc. are small primes
 * to help give the perlin noise a bit more of a random feel.
 */
static void HeightMapCoastLines(uint8 water_borders)
{
	int smallest_size = min(_settings_game.game_creation.map_x, _settings_game.game_creation.map_y);
	const int margin = 4;
	uint y, x;
	double max_x;
	double max_y;

	/* Lower to sea level */
	for (y = 0; y <= _height_map.size_y; y++) {
		if (HasBit(water_borders, BORDER_NE)) {
			/* Top right */
			max_x = abs((perlin_coast_noise_2D(_height_map.size_y - y, y, 0.9, 53) + 0.25) * 5 + (perlin_coast_noise_2D(y, y, 0.35, 179) + 1) * 12);
			max_x = max((smallest_size * smallest_size / 16) + max_x, (smallest_size * smallest_size / 16) + margin - max_x);
			if (smallest_size < 8 && max_x > 5) max_x /= 1.5;
			for (x = 0; x < max_x; x++) {
				_height_map.height(x, y) = 0;
			}
		}

		if (HasBit(water_borders, BORDER_SW)) {
			/* Bottom left */
			max_x = abs((perlin_coast_noise_2D(_height_map.size_y - y, y, 0.85, 101) + 0.3) * 6 + (perlin_coast_noise_2D(y, y, 0.45,  67) + 0.75) * 8);
			max_x = max((smallest_size * smallest_size / 16) + max_x, (smallest_size * smallest_size / 16) + margin - max_x);
			if (smallest_size < 8 && max_x > 5) max_x /= 1.5;
			for (x = _height_map.size_x; x > (_height_map.size_x - 1 - max_x); x--) {
				_height_map.height(x, y) = 0;
			}
		}
	}

	/* Lower to sea level */
	for (x = 0; x <= _height_map.size_x; x++) {
		if (HasBit(water_borders, BORDER_NW)) {
			/* Top left */
			max_y = abs((perlin_coast_noise_2D(x, _height_map.size_y / 2, 0.9, 167) + 0.4) * 5 + (perlin_coast_noise_2D(x, _height_map.size_y / 3, 0.4, 211) + 0.7) * 9);
			max_y = max((smallest_size * smallest_size / 16) + max_y, (smallest_size * smallest_size / 16) + margin - max_y);
			if (smallest_size < 8 && max_y > 5) max_y /= 1.5;
			for (y = 0; y < max_y; y++) {
				_height_map.height(x, y) = 0;
			}
		}

		if (HasBit(water_borders, BORDER_SE)) {
			/* Bottom right */
			max_y = abs((perlin_coast_noise_2D(x, _height_map.size_y / 3, 0.85, 71) + 0.25) * 6 + (perlin_coast_noise_2D(x, _height_map.size_y / 3, 0.35, 193) + 0.75) * 12);
			max_y = max((smallest_size * smallest_size / 16) + max_y, (smallest_size * smallest_size / 16) + margin - max_y);
			if (smallest_size < 8 && max_y > 5) max_y /= 1.5;
			for (y = _height_map.size_y; y > (_height_map.size_y - 1 - max_y); y--) {
				_height_map.height(x, y) = 0;
			}
		}
	}
}

/** Start at given point, move in given direction, find and Smooth coast in that direction */
static void HeightMapSmoothCoastInDirection(int org_x, int org_y, int dir_x, int dir_y)
{
	const int max_coast_dist_from_edge = 35;
	const int max_coast_Smooth_depth = 35;

	int x, y;
	int ed; // coast distance from edge
	int depth;

	height_t h_prev = 16;
	height_t h;

	assert(IsValidXY(org_x, org_y));

	/* Search for the coast (first non-water tile) */
	for (x = org_x, y = org_y, ed = 0; IsValidXY(x, y) && ed < max_coast_dist_from_edge; x += dir_x, y += dir_y, ed++) {
		/* Coast found? */
		if (_height_map.height(x, y) > 15) break;

		/* Coast found in the neighborhood? */
		if (IsValidXY(x + dir_y, y + dir_x) && _height_map.height(x + dir_y, y + dir_x) > 0) break;

		/* Coast found in the neighborhood on the other side */
		if (IsValidXY(x - dir_y, y - dir_x) && _height_map.height(x - dir_y, y - dir_x) > 0) break;
	}

	/* Coast found or max_coast_dist_from_edge has been reached.
	 * Soften the coast slope */
	for (depth = 0; IsValidXY(x, y) && depth <= max_coast_Smooth_depth; depth++, x += dir_x, y += dir_y) {
		h = _height_map.height(x, y);
		h = min(h, h_prev + (4 + depth)); // coast softening formula
		_height_map.height(x, y) = h;
		h_prev = h;
	}
}

/** Smooth coasts by modulating height of tiles close to map edges with cosine of distance from edge */
static void HeightMapSmoothCoasts(uint8 water_borders)
{
	uint x, y;
	/* First Smooth NW and SE coasts (y close to 0 and y close to size_y) */
	for (x = 0; x < _height_map.size_x; x++) {
		if (HasBit(water_borders, BORDER_NW)) HeightMapSmoothCoastInDirection(x, 0, 0, 1);
		if (HasBit(water_borders, BORDER_SE)) HeightMapSmoothCoastInDirection(x, _height_map.size_y - 1, 0, -1);
	}
	/* First Smooth NE and SW coasts (x close to 0 and x close to size_x) */
	for (y = 0; y < _height_map.size_y; y++) {
		if (HasBit(water_borders, BORDER_NE)) HeightMapSmoothCoastInDirection(0, y, 1, 0);
		if (HasBit(water_borders, BORDER_SW)) HeightMapSmoothCoastInDirection(_height_map.size_x - 1, y, -1, 0);
	}
}

/**
 * This routine provides the essential cleanup necessary before OTTD can
 * display the terrain. When generated, the terrain heights can jump more than
 * one level between tiles. This routine smooths out those differences so that
 * the most it can change is one level. When OTTD can support cliffs, this
 * routine may not be necessary.
 */
static void HeightMapSmoothSlopes(height_t dh_max)
{
	int x, y;
	for (y = 0; y <= (int)_height_map.size_y; y++) {
		for (x = 0; x <= (int)_height_map.size_x; x++) {
			height_t h_max = min(_height_map.height(x > 0 ? x - 1 : x, y), _height_map.height(x, y > 0 ? y - 1 : y)) + dh_max;
			if (_height_map.height(x, y) > h_max) _height_map.height(x, y) = h_max;
		}
	}
	for (y = _height_map.size_y; y >= 0; y--) {
		for (x = _height_map.size_x; x >= 0; x--) {
			height_t h_max = min(_height_map.height((uint)x < _height_map.size_x ? x + 1 : x, y), _height_map.height(x, (uint)y < _height_map.size_y ? y + 1 : y)) + dh_max;
			if (_height_map.height(x, y) > h_max) _height_map.height(x, y) = h_max;
		}
	}
}

/**
 * Height map terraform post processing:
 *  - water level adjusting
 *  - coast Smoothing
 *  - slope Smoothing
 *  - height histogram redistribution by sine wave transform
 */
static void HeightMapNormalize()
{
	int sea_level_setting = _settings_game.difficulty.quantity_sea_lakes;
	const amplitude_t water_percent = sea_level_setting != (int)CUSTOM_SEA_LEVEL_NUMBER_DIFFICULTY ? _water_percent[sea_level_setting] : _settings_game.game_creation.custom_sea_level * 1024 / 100;
	const height_t h_max_new = I2H(_max_height[_settings_game.difficulty.terrain_type]);
	const height_t roughness = 7 + 3 * _settings_game.game_creation.tgen_smoothness;

	HeightMapAdjustWaterLevel(water_percent, h_max_new);

	byte water_borders = _settings_game.construction.freeform_edges ? _settings_game.game_creation.water_borders : 0xF;
	if (water_borders == BORDERS_RANDOM) water_borders = GB(Random(), 0, 4);

	HeightMapCoastLines(water_borders);
	HeightMapSmoothSlopes(roughness);

	HeightMapSmoothCoasts(water_borders);
	HeightMapSmoothSlopes(roughness);

	HeightMapSineTransform(12, h_max_new);

	if (_settings_game.game_creation.variety > 0) {
		HeightMapCurves(_settings_game.game_creation.variety);
	}

	HeightMapSmoothSlopes(16);
}

/**
 * The Perlin Noise calculation using large primes
 * The initial number is adjusted by two values; the generation_seed, and the
 * passed parameter; prime.
 * prime is used to allow the perlin noise generator to create useful random
 * numbers from slightly different series.
 */
static double int_noise(const long x, const long y, const int prime)
{
	long n = x + y * prime + _settings_game.game_creation.generation_seed;

	n = (n << 13) ^ n;

	/* Pseudo-random number generator, using several large primes */
	return 1.0 - (double)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0;
}


/**
 * This routine determines the interpolated value between a and b
 */
static inline double linear_interpolate(const double a, const double b, const double x)
{
	return a + x * (b - a);
}


/**
 * This routine returns the smoothed interpolated noise for an x and y, using
 * the values from the surrounding positions.
 */
static double interpolated_noise(const double x, const double y, const int prime)
{
	const int integer_X = (int)x;
	const int integer_Y = (int)y;

	const double fractional_X = x - (double)integer_X;
	const double fractional_Y = y - (double)integer_Y;

	const double v1 = int_noise(integer_X,     integer_Y,     prime);
	const double v2 = int_noise(integer_X + 1, integer_Y,     prime);
	const double v3 = int_noise(integer_X,     integer_Y + 1, prime);
	const double v4 = int_noise(integer_X + 1, integer_Y + 1, prime);

	const double i1 = linear_interpolate(v1, v2, fractional_X);
	const double i2 = linear_interpolate(v3, v4, fractional_X);

	return linear_interpolate(i1, i2, fractional_Y);
}


/**
 * This is a similar function to the main perlin noise calculation, but uses
 * the value p passed as a parameter rather than selected from the predefined
 * sequences. as you can guess by its title, i use this to create the indented
 * coastline, which is just another perlin sequence.
 */
static double perlin_coast_noise_2D(const double x, const double y, const double p, const int prime)
{
	double total = 0.0;
	int i;

	for (i = 0; i < 6; i++) {
		const double frequency = (double)(1 << i);
		const double amplitude = pow(p, (double)i);

		total += interpolated_noise((x * frequency) / 64.0, (y * frequency) / 64.0, prime) * amplitude;
	}

	return total;
}


/** A small helper function to initialize the terrain */
static void TgenSetTileHeight(TileIndex tile, int height)
{
	SetTileHeight(tile, height);

	/* Only clear the tiles within the map area. */
	if (TileX(tile) != MapMaxX() && TileY(tile) != MapMaxY() &&
			(!_settings_game.construction.freeform_edges || (TileX(tile) != 0 && TileY(tile) != 0))) {
		MakeClear(tile, CLEAR_GRASS, 3);
	}
}

/**
 * The main new land generator using Perlin noise. Desert landscape is handled
 * different to all others to give a desert valley between two high mountains.
 * Clearly if a low height terrain (flat/very flat) is chosen, then the tropic
 * areas wont be high enough, and there will be very little tropic on the map.
 * Thus Tropic works best on Hilly or Mountainous.
 */
void GenerateTerrainPerlin()
{
	uint x, y;

	if (!AllocHeightMap()) return;
	GenerateWorldSetAbortCallback(FreeHeightMap);

	HeightMapGenerate();

	IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

	HeightMapNormalize();

	IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

	/* First make sure the tiles at the north border are void tiles if needed. */
	if (_settings_game.construction.freeform_edges) {
		for (y = 0; y < _height_map.size_y - 1; y++) MakeVoid(_height_map.size_x * y);
		for (x = 0; x < _height_map.size_x;     x++) MakeVoid(x);
	}

	/* Transfer height map into OTTD map */
	for (y = 0; y < _height_map.size_y; y++) {
		for (x = 0; x < _height_map.size_x; x++) {
			int height = H2I(_height_map.height(x, y));
			if (height < 0) height = 0;
			if (height > 15) height = 15;
			TgenSetTileHeight(TileXY(x, y), height);
		}
	}

	IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

	FreeHeightMap();
	GenerateWorldSetAbortCallback(NULL);
}

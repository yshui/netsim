#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "gaussian.h"

#define TWO_PI 6.2831853071795864769252866

//From: https://en.wikipedia.org/wiki/Box%E2%80%93Muller_transform

static inline double gaussian_noise(double variance, double mean) {
	static bool has_spare = false;
	static double rand1, rand2;

	if (has_spare) {
		has_spare = false;
		return sqrt(variance*rand1)*sin(rand2)+mean;
	}

	has_spare = true;

	rand1 = random()/((double)RAND_MAX);
	if (rand1 < 1e-100)
		rand1 = 1e-100;
	rand1 = -2*log(rand1);
	rand2 = (random()/((double)RAND_MAX))*TWO_PI;

	return sqrt(variance*rand1)*cos(rand2)+mean;
}

double gaussian_noise_nz(double variance, double mean) {
	double tmp = gaussian_noise(variance*variance, mean);
	if (tmp < 0)
		return 0;
	return tmp;
}

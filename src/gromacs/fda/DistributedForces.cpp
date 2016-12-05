/*
 * DistributedForces.cpp
 *
 *  Created on: Nov 3, 2016
 *      Author: Bernd Doser, HITS gGmbH <bernd.doser@h-its.org>
 */

#include "DistributedForces.h"
#include "gromacs/utility/fatalerror.h"
#include "Utilities.h"

using namespace fda;

void DistributedForces::scalar_real_divide(real divisor)
{
  real inv = 1 / divisor;
  for (auto& si : scalar)
    for (auto& sj : si.second) sj.second.force *= inv;
}

void DistributedForces::summed_merge_to_scalar(const rvec *x, Vector2Scalar v2s)
{
  for (auto& si : summed) {
	int i = si.first;
    for (auto& sj : si.second) {
      int j = sj.first;
      scalar[i][j].force += vector2signedscalar(sj.second.force.get_pointer(), x[i], x[j], v2s);
    }
  }
}
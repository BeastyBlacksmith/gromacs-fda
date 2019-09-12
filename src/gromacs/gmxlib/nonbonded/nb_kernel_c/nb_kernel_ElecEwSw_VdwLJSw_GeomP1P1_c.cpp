/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2012,2013,2014.2015,2017,2018, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
/*
 * Note: this file was generated by the GROMACS c kernel generator.
 */
#include "gmxpre.h"
#include "config.h"
#include <math.h>

#include "../nb_kernel.h"
#include "gromacs/fda/FDA.h"
#include "gromacs/gmxlib/nrnb.h"

/*
 * Gromacs nonbonded kernel:   nb_kernel_ElecEwSw_VdwLJSw_GeomP1P1_VF_c
 * Electrostatics interaction: Ewald
 * VdW interaction:            LennardJones
 * Geometry:                   Particle-Particle
 * Calculate force/pot:        PotentialAndForce
 */
void
nb_kernel_ElecEwSw_VdwLJSw_GeomP1P1_VF_c
                    (t_nblist                    * gmx_restrict       nlist,
                     rvec                        * gmx_restrict          xx,
                     rvec                        * gmx_restrict          ff,
                     struct t_forcerec           * gmx_restrict          fr,
                     t_mdatoms                   * gmx_restrict     mdatoms,
                     nb_kernel_data_t gmx_unused * gmx_restrict kernel_data,
                     t_nrnb                      * gmx_restrict        nrnb)
{
    int              i_shift_offset,i_coord_offset,j_coord_offset;
    int              j_index_start,j_index_end;
    int              nri,inr,ggid,iidx,jidx,jnr,outeriter,inneriter;
    real             shX,shY,shZ,tx,ty,tz,fscal,rcutoff,rcutoff2;
    int              *iinr,*jindex,*jjnr,*shiftidx,*gid;
    real             *shiftvec,*fshift,*x,*f;
    int              vdwioffset0;
    real             ix0,iy0,iz0,fix0,fiy0,fiz0,iq0,isai0;
    int              vdwjidx0;
    real             jx0,jy0,jz0,fjx0,fjy0,fjz0,jq0,isaj0;
    real             dx00,dy00,dz00,rsq00,rinv00,rinvsq00,r00,qq00,c6_00,c12_00,cexp1_00,cexp2_00;
    real             velec,felec,velecsum,facel,crf,krf,krf2;
    real             *charge;
    int              nvdwtype;
    real             rinvsix,rvdw,vvdw,vvdw6,vvdw12,fvdw,fvdw6,fvdw12,vvdwsum,br,vvdwexp,sh_vdw_invrcut6;
    int              *vdwtype;
    real             *vdwparam;
    int              ewitab;
    real             ewtabscale,eweps,sh_ewald,ewrt,ewtabhalfspace;
    real             *ewtab;
    real             rswitch,swV3,swV4,swV5,swF2,swF3,swF4,d,d2,sw,dsw;

    FDA *fda = fr->fda;

    x                = xx[0];
    f                = ff[0];

    nri              = nlist->nri;
    iinr             = nlist->iinr;
    jindex           = nlist->jindex;
    jjnr             = nlist->jjnr;
    shiftidx         = nlist->shift;
    gid              = nlist->gid;
    shiftvec         = fr->shift_vec[0];
    fshift           = fr->fshift[0];
    facel            = fr->ic->epsfac;
    charge           = mdatoms->chargeA;
    nvdwtype         = fr->ntype;
    vdwparam         = fr->nbfp;
    vdwtype          = mdatoms->typeA;

    sh_ewald         = fr->ic->sh_ewald;
    ewtab            = fr->ic->tabq_coul_FDV0;
    ewtabscale       = fr->ic->tabq_scale;
    ewtabhalfspace   = 0.5/ewtabscale;

    /* When we use explicit cutoffs the value must be identical for elec and VdW, so use elec as an arbitrary choice */
    rcutoff          = fr->ic->rcoulomb;
    rcutoff2         = rcutoff*rcutoff;

    rswitch          = fr->ic->rcoulomb_switch;
    /* Setup switch parameters */
    d                = rcutoff-rswitch;
    swV3             = -10.0/(d*d*d);
    swV4             =  15.0/(d*d*d*d);
    swV5             =  -6.0/(d*d*d*d*d);
    swF2             = -30.0/(d*d*d);
    swF3             =  60.0/(d*d*d*d);
    swF4             = -30.0/(d*d*d*d*d);

    outeriter        = 0;
    inneriter        = 0;

    /* Start outer loop over neighborlists */
    for(iidx=0; iidx<nri; iidx++)
    {
        /* Load shift vector for this list */
        i_shift_offset   = DIM*shiftidx[iidx];
        shX              = shiftvec[i_shift_offset+XX];
        shY              = shiftvec[i_shift_offset+YY];
        shZ              = shiftvec[i_shift_offset+ZZ];

        /* Load limits for loop over neighbors */
        j_index_start    = jindex[iidx];
        j_index_end      = jindex[iidx+1];

        /* Get outer coordinate index */
        inr              = iinr[iidx];
        i_coord_offset   = DIM*inr;

        /* Load i particle coords and add shift vector */
        ix0              = shX + x[i_coord_offset+DIM*0+XX];
        iy0              = shY + x[i_coord_offset+DIM*0+YY];
        iz0              = shZ + x[i_coord_offset+DIM*0+ZZ];

        fix0             = 0.0;
        fiy0             = 0.0;
        fiz0             = 0.0;

        /* Load parameters for i particles */
        iq0              = facel*charge[inr+0];
        vdwioffset0      = 2*nvdwtype*vdwtype[inr+0];

        /* Reset potential sums */
        velecsum         = 0.0;
        vvdwsum          = 0.0;

        /* Start inner kernel loop */
        for(jidx=j_index_start; jidx<j_index_end; jidx++)
        {
            /* Get j neighbor index, and coordinate index */
            jnr              = jjnr[jidx];
            j_coord_offset   = DIM*jnr;

            /* load j atom coordinates */
            jx0              = x[j_coord_offset+DIM*0+XX];
            jy0              = x[j_coord_offset+DIM*0+YY];
            jz0              = x[j_coord_offset+DIM*0+ZZ];

            /* Calculate displacement vector */
            dx00             = ix0 - jx0;
            dy00             = iy0 - jy0;
            dz00             = iz0 - jz0;

            /* Calculate squared distance and things based on it */
            rsq00            = dx00*dx00+dy00*dy00+dz00*dz00;

            rinv00           = 1.0/sqrt(rsq00);

            rinvsq00         = rinv00*rinv00;

            /* Load parameters for j particles */
            jq0              = charge[jnr+0];
            vdwjidx0         = 2*vdwtype[jnr+0];

            /**************************
             * CALCULATE INTERACTIONS *
             **************************/

            if (rsq00<rcutoff2)
            {

            r00              = rsq00*rinv00;

            qq00             = iq0*jq0;
            c6_00            = vdwparam[vdwioffset0+vdwjidx0];
            c12_00           = vdwparam[vdwioffset0+vdwjidx0+1];

            /* EWALD ELECTROSTATICS */

            /* Calculate Ewald table index by multiplying r with scale and truncate to integer */
            ewrt             = r00*ewtabscale;
            ewitab           = ewrt;
            eweps            = ewrt-ewitab;
            ewitab           = 4*ewitab;
            felec            = ewtab[ewitab]+eweps*ewtab[ewitab+1];
            velec            = qq00*(rinv00-(ewtab[ewitab+2]-ewtabhalfspace*eweps*(ewtab[ewitab]+felec)));
            felec            = qq00*rinv00*(rinvsq00-felec);

            /* LENNARD-JONES DISPERSION/REPULSION */

            rinvsix          = rinvsq00*rinvsq00*rinvsq00;
            vvdw6            = c6_00*rinvsix;
            vvdw12           = c12_00*rinvsix*rinvsix;
            vvdw             = vvdw12*(1.0/12.0) - vvdw6*(1.0/6.0);
            fvdw             = (vvdw12-vvdw6)*rinvsq00;

            d                = r00-rswitch;
            d                = (d>0.0) ? d : 0.0;
            d2               = d*d;
            sw               = 1.0+d2*d*(swV3+d*(swV4+d*swV5));

            dsw              = d2*(swF2+d*(swF3+d*swF4));

            /* Evaluate switch function */
            /* fscal'=f'/r=-(v*sw)'/r=-(v'*sw+v*dsw)/r=-v'*sw/r-v*dsw/r=fscal*sw-v*dsw/r */
            felec            = felec*sw - rinv00*velec*dsw;
            fvdw             = fvdw*sw - rinv00*vvdw*dsw;
            velec           *= sw;
            vvdw            *= sw;

            /* Update potential sums from outer loop */
            velecsum        += velec;
            vvdwsum         += vvdw;

            fscal            = felec+fvdw;

            /* Calculate temporary vectorial force */
            tx               = fscal*dx00;
            ty               = fscal*dy00;
            tz               = fscal*dz00;

            /* Update vectorial force */
            fix0            += tx;
            fiy0            += ty;
            fiz0            += tz;
            f[j_coord_offset+DIM*0+XX] -= tx;
            f[j_coord_offset+DIM*0+YY] -= ty;
            f[j_coord_offset+DIM*0+ZZ] -= tz;

            /* pairwise forces */
            fda_add_nonbonded(fda, inr+0, jnr+0, felec, fvdw, dx00, dy00, dz00);
            fda_virial_bond(fda, inr+0, jnr+0, fscal, dx00, dy00, dz00);

            }

            /* Inner loop uses 75 flops */
        }
        /* End of innermost loop */

        tx = ty = tz = 0;
        f[i_coord_offset+DIM*0+XX] += fix0;
        f[i_coord_offset+DIM*0+YY] += fiy0;
        f[i_coord_offset+DIM*0+ZZ] += fiz0;
        tx                         += fix0;
        ty                         += fiy0;
        tz                         += fiz0;
        fshift[i_shift_offset+XX]  += tx;
        fshift[i_shift_offset+YY]  += ty;
        fshift[i_shift_offset+ZZ]  += tz;

        ggid                        = gid[iidx];
        /* Update potential energies */
        kernel_data->energygrp_elec[ggid] += velecsum;
        kernel_data->energygrp_vdw[ggid] += vvdwsum;

        /* Increment number of inner iterations */
        inneriter                  += j_index_end - j_index_start;

        /* Outer loop uses 15 flops */
    }

    /* Increment number of outer iterations */
    outeriter        += nri;

    /* Update outer/inner flops */

    inc_nrnb(nrnb,eNR_NBKERNEL_ELEC_VDW_VF,outeriter*15 + inneriter*75);
}
/*
 * Gromacs nonbonded kernel:   nb_kernel_ElecEwSw_VdwLJSw_GeomP1P1_F_c
 * Electrostatics interaction: Ewald
 * VdW interaction:            LennardJones
 * Geometry:                   Particle-Particle
 * Calculate force/pot:        Force
 */
void
nb_kernel_ElecEwSw_VdwLJSw_GeomP1P1_F_c
                    (t_nblist                    * gmx_restrict       nlist,
                     rvec                        * gmx_restrict          xx,
                     rvec                        * gmx_restrict          ff,
                     struct t_forcerec           * gmx_restrict          fr,
                     t_mdatoms                   * gmx_restrict     mdatoms,
                     nb_kernel_data_t gmx_unused * gmx_restrict kernel_data,
                     t_nrnb                      * gmx_restrict        nrnb)
{
    int              i_shift_offset,i_coord_offset,j_coord_offset;
    int              j_index_start,j_index_end;
    int              nri,inr,ggid,iidx,jidx,jnr,outeriter,inneriter;
    real             shX,shY,shZ,tx,ty,tz,fscal,rcutoff,rcutoff2;
    int              *iinr,*jindex,*jjnr,*shiftidx,*gid;
    real             *shiftvec,*fshift,*x,*f;
    int              vdwioffset0;
    real             ix0,iy0,iz0,fix0,fiy0,fiz0,iq0,isai0;
    int              vdwjidx0;
    real             jx0,jy0,jz0,fjx0,fjy0,fjz0,jq0,isaj0;
    real             dx00,dy00,dz00,rsq00,rinv00,rinvsq00,r00,qq00,c6_00,c12_00,cexp1_00,cexp2_00;
    real             velec,felec,velecsum,facel,crf,krf,krf2;
    real             *charge;
    int              nvdwtype;
    real             rinvsix,rvdw,vvdw,vvdw6,vvdw12,fvdw,fvdw6,fvdw12,vvdwsum,br,vvdwexp,sh_vdw_invrcut6;
    int              *vdwtype;
    real             *vdwparam;
    int              ewitab;
    real             ewtabscale,eweps,sh_ewald,ewrt,ewtabhalfspace;
    real             *ewtab;
    real             rswitch,swV3,swV4,swV5,swF2,swF3,swF4,d,d2,sw,dsw;

    FDA *fda = fr->fda;

    x                = xx[0];
    f                = ff[0];

    nri              = nlist->nri;
    iinr             = nlist->iinr;
    jindex           = nlist->jindex;
    jjnr             = nlist->jjnr;
    shiftidx         = nlist->shift;
    gid              = nlist->gid;
    shiftvec         = fr->shift_vec[0];
    fshift           = fr->fshift[0];
    facel            = fr->ic->epsfac;
    charge           = mdatoms->chargeA;
    nvdwtype         = fr->ntype;
    vdwparam         = fr->nbfp;
    vdwtype          = mdatoms->typeA;

    sh_ewald         = fr->ic->sh_ewald;
    ewtab            = fr->ic->tabq_coul_FDV0;
    ewtabscale       = fr->ic->tabq_scale;
    ewtabhalfspace   = 0.5/ewtabscale;

    /* When we use explicit cutoffs the value must be identical for elec and VdW, so use elec as an arbitrary choice */
    rcutoff          = fr->ic->rcoulomb;
    rcutoff2         = rcutoff*rcutoff;

    rswitch          = fr->ic->rcoulomb_switch;
    /* Setup switch parameters */
    d                = rcutoff-rswitch;
    swV3             = -10.0/(d*d*d);
    swV4             =  15.0/(d*d*d*d);
    swV5             =  -6.0/(d*d*d*d*d);
    swF2             = -30.0/(d*d*d);
    swF3             =  60.0/(d*d*d*d);
    swF4             = -30.0/(d*d*d*d*d);

    outeriter        = 0;
    inneriter        = 0;

    /* Start outer loop over neighborlists */
    for(iidx=0; iidx<nri; iidx++)
    {
        /* Load shift vector for this list */
        i_shift_offset   = DIM*shiftidx[iidx];
        shX              = shiftvec[i_shift_offset+XX];
        shY              = shiftvec[i_shift_offset+YY];
        shZ              = shiftvec[i_shift_offset+ZZ];

        /* Load limits for loop over neighbors */
        j_index_start    = jindex[iidx];
        j_index_end      = jindex[iidx+1];

        /* Get outer coordinate index */
        inr              = iinr[iidx];
        i_coord_offset   = DIM*inr;

        /* Load i particle coords and add shift vector */
        ix0              = shX + x[i_coord_offset+DIM*0+XX];
        iy0              = shY + x[i_coord_offset+DIM*0+YY];
        iz0              = shZ + x[i_coord_offset+DIM*0+ZZ];

        fix0             = 0.0;
        fiy0             = 0.0;
        fiz0             = 0.0;

        /* Load parameters for i particles */
        iq0              = facel*charge[inr+0];
        vdwioffset0      = 2*nvdwtype*vdwtype[inr+0];

        /* Start inner kernel loop */
        for(jidx=j_index_start; jidx<j_index_end; jidx++)
        {
            /* Get j neighbor index, and coordinate index */
            jnr              = jjnr[jidx];
            j_coord_offset   = DIM*jnr;

            /* load j atom coordinates */
            jx0              = x[j_coord_offset+DIM*0+XX];
            jy0              = x[j_coord_offset+DIM*0+YY];
            jz0              = x[j_coord_offset+DIM*0+ZZ];

            /* Calculate displacement vector */
            dx00             = ix0 - jx0;
            dy00             = iy0 - jy0;
            dz00             = iz0 - jz0;

            /* Calculate squared distance and things based on it */
            rsq00            = dx00*dx00+dy00*dy00+dz00*dz00;

            rinv00           = 1.0/sqrt(rsq00);

            rinvsq00         = rinv00*rinv00;

            /* Load parameters for j particles */
            jq0              = charge[jnr+0];
            vdwjidx0         = 2*vdwtype[jnr+0];

            /**************************
             * CALCULATE INTERACTIONS *
             **************************/

            if (rsq00<rcutoff2)
            {

            r00              = rsq00*rinv00;

            qq00             = iq0*jq0;
            c6_00            = vdwparam[vdwioffset0+vdwjidx0];
            c12_00           = vdwparam[vdwioffset0+vdwjidx0+1];

            /* EWALD ELECTROSTATICS */

            /* Calculate Ewald table index by multiplying r with scale and truncate to integer */
            ewrt             = r00*ewtabscale;
            ewitab           = ewrt;
            eweps            = ewrt-ewitab;
            ewitab           = 4*ewitab;
            felec            = ewtab[ewitab]+eweps*ewtab[ewitab+1];
            velec            = qq00*(rinv00-(ewtab[ewitab+2]-ewtabhalfspace*eweps*(ewtab[ewitab]+felec)));
            felec            = qq00*rinv00*(rinvsq00-felec);

            /* LENNARD-JONES DISPERSION/REPULSION */

            rinvsix          = rinvsq00*rinvsq00*rinvsq00;
            vvdw6            = c6_00*rinvsix;
            vvdw12           = c12_00*rinvsix*rinvsix;
            vvdw             = vvdw12*(1.0/12.0) - vvdw6*(1.0/6.0);
            fvdw             = (vvdw12-vvdw6)*rinvsq00;

            d                = r00-rswitch;
            d                = (d>0.0) ? d : 0.0;
            d2               = d*d;
            sw               = 1.0+d2*d*(swV3+d*(swV4+d*swV5));

            dsw              = d2*(swF2+d*(swF3+d*swF4));

            /* Evaluate switch function */
            /* fscal'=f'/r=-(v*sw)'/r=-(v'*sw+v*dsw)/r=-v'*sw/r-v*dsw/r=fscal*sw-v*dsw/r */
            felec            = felec*sw - rinv00*velec*dsw;
            fvdw             = fvdw*sw - rinv00*vvdw*dsw;

            fscal            = felec+fvdw;

            /* Calculate temporary vectorial force */
            tx               = fscal*dx00;
            ty               = fscal*dy00;
            tz               = fscal*dz00;

            /* Update vectorial force */
            fix0            += tx;
            fiy0            += ty;
            fiz0            += tz;
            f[j_coord_offset+DIM*0+XX] -= tx;
            f[j_coord_offset+DIM*0+YY] -= ty;
            f[j_coord_offset+DIM*0+ZZ] -= tz;

            /* pairwise forces */
            fda_add_nonbonded(fda, inr+0, jnr+0, felec, fvdw, dx00, dy00, dz00);
            fda_virial_bond(fda, inr+0, jnr+0, fscal, dx00, dy00, dz00);

            }

            /* Inner loop uses 71 flops */
        }
        /* End of innermost loop */

        tx = ty = tz = 0;
        f[i_coord_offset+DIM*0+XX] += fix0;
        f[i_coord_offset+DIM*0+YY] += fiy0;
        f[i_coord_offset+DIM*0+ZZ] += fiz0;
        tx                         += fix0;
        ty                         += fiy0;
        tz                         += fiz0;
        fshift[i_shift_offset+XX]  += tx;
        fshift[i_shift_offset+YY]  += ty;
        fshift[i_shift_offset+ZZ]  += tz;

        /* Increment number of inner iterations */
        inneriter                  += j_index_end - j_index_start;

        /* Outer loop uses 13 flops */
    }

    /* Increment number of outer iterations */
    outeriter        += nri;

    /* Update outer/inner flops */

    inc_nrnb(nrnb,eNR_NBKERNEL_ELEC_VDW_F,outeriter*13 + inneriter*71);
}

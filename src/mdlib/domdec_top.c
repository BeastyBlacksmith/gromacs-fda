#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "typedefs.h"
#include "smalloc.h"
#include "domdec.h"
#include "domdec_network.h"
#include "names.h"
#include "network.h"
#include "vec.h"
#include "pbc.h"
#include "gmx_random.h"
#include "topsort.h"
#include "mtop_util.h"

typedef struct {
  int  *index;  /* Index for each atom into il                  */ 
  int  *il;     /* ftype|type|a0|...|an|ftype|...               */
} gmx_reverse_ilist_t;

typedef struct {
  int  a_start;
  int  a_end;
  int  natoms_mol;
  int  type;
} gmx_molblock_ind_t;

typedef struct gmx_reverse_top {
  bool bConstr; /* Are there constraint in this revserse top?   */
  bool bBCheck; /* All bonded interactions have to be assigned? */
  bool bMultiCGmols; /* Are the multi charge-group molecules?   */
  gmx_reverse_ilist_t *ril_mt; /* Reverse ilist for all moltypes */
  int  ril_mt_tot_size;
  int  ilsort;  /* The sorting state of bondeds for free energy */
  gmx_molblock_ind_t *mbi;
} gmx_reverse_top_t;

/* Static pointers only used for an error message */
static gmx_mtop_t *err_top_global;
static t_topology *err_top_local;

static int nral_rt(int ftype)
{
  /* Returns the number of atom entries for il in gmx_reverse_top_t */
  int nral;

  nral = NRAL(ftype);
  if (interaction_function[ftype].flags & IF_VSITE) {
    /* With vsites the reverse topology contains two extra entries for PBC */
    nral += 2;
  }

  return nral;
}

#ifdef HAVE_TO_UPDATE
static void print_missing_interaction_atoms(FILE *fplog,t_commrec *cr,
					    gmx_mtop_t *mtop,t_idef *idef)
{
  gmx_reverse_top_t *rt;
  int *assigned,*gatindex,ftype,ftype_j,nral,i,j,k,a0,a;
  int nprint;
  t_ilist *il;
  t_iatom *ia;
  bool bFound;
  
  rt = cr->dd->reverse_top;

  snew(assigned,rt->index[natoms]);
  
  gatindex = cr->dd->gatindex;
  for(ftype=0; ftype<F_NRE; ftype++) {
    if (((interaction_function[ftype].flags & IF_BOND) &&
	 !(interaction_function[ftype].flags & IF_VSITE) &&
	 (rt->bBCheck || !(interaction_function[ftype].flags & IF_LIMZERO)))
	|| ftype == F_SETTLE || (rt->bConstr && ftype == F_CONSTR)) {
      nral = NRAL(ftype);
      il = &idef->il[ftype];
      ia = il->iatoms;
      for(i=0; i<il->nr; i+=1+nral) {
	a0 = gatindex[ia[1]];
	j = rt->index[a0];
	bFound = FALSE;
	while (j < rt->index[a0+1] && !bFound) {
	  ftype_j = rt->il[j];
	  /* Here we need to check if this interaction has not already
	   * been assigned, since we could have multiply defined interactions.
	   */
	  if (ftype == ftype_j && ia[0] == rt->il[j+1] && assigned[j] == 0) {
	    /* Check the atoms */
	    bFound = TRUE;
	    for(a=0; a<nral; a++) {
	      if (gatindex[ia[1+a]] != rt->il[j+2+a])
		bFound = FALSE;
	    }
	    if (bFound) {
	      assigned[j] = 1;
	    }
	  }
	  j += 2 + nral_rt(ftype_j);
	}
	if (!bFound)
	  gmx_incons("Some interactions seem to be assigned multiple times\n");
	ia += 1 + nral;
      }
    }
  }

  gmx_sumi(rt->index[natoms],assigned,cr);
  
  nprint = 10;
  if (DDMASTER(cr->dd)) {
    fprintf(fplog,
	    "\nThe first %d missing interactions, except for exclusions:\n",
	    nprint);
    fprintf(stderr,
	    "\nThe first %d missing interactions, except for exclusions:\n",
	    nprint);
  }
  i = 0;
  j = 0;
  while (j < rt->index[natoms]) {
    ftype = rt->il[j];
    nral  = NRAL(ftype);
    if (assigned[j] == 0 &&
	!(interaction_function[ftype].flags & IF_VSITE)) {
      if (DDMASTER(cr->dd)) {
	fprintf(fplog, "%20s atoms",interaction_function[ftype].longname);
	fprintf(stderr,"%20s atoms",interaction_function[ftype].longname);
	for(a=0; a<nral; a++) {
	  fprintf(fplog, "%5d",rt->il[j+2+a]+1);
	  fprintf(stderr,"%5d",rt->il[j+2+a]+1);
	}
	fprintf(fplog, "\n");
	fprintf(stderr,"\n");
      }
      i++;
      if (i >= nprint)
	break;
    }
    j += 2 + nral_rt(ftype);
  }
  
  sfree(assigned);    
}
#endif

void dd_print_missing_interactions(FILE *fplog,t_commrec *cr,int local_count)
{
  int  ndiff_tot,cl[F_NRE],n,ndiff,rest_global,rest_local;
  int  ftype,nral;
  char buf[STRLEN];
  gmx_domdec_t *dd;

  dd = cr->dd;

  if (fplog) {
    fprintf(fplog,"\nNot all bonded interactions have been properly assigned to the domain decomposition cells\n");
    fflush(fplog);
  }

  ndiff_tot = local_count - dd->nbonded_global;

  for(ftype=0; ftype<F_NRE; ftype++) {
    nral = NRAL(ftype);
    cl[ftype] = err_top_local->idef.il[ftype].nr/(1+nral);
  }

  gmx_sumi(F_NRE,cl,cr);
  
  if (DDMASTER(dd)) {
    fprintf(fplog,"\nA list of missing interactions:\n");
    fprintf(stderr,"\nA list of missing interactions:\n");
    rest_global = dd->nbonded_global;
    rest_local  = local_count;
    for(ftype=0; ftype<F_NRE; ftype++) {
      if (((interaction_function[ftype].flags & IF_BOND) &&
	   (dd->reverse_top->bBCheck 
	    || !(interaction_function[ftype].flags & IF_LIMZERO)))
	  || ftype == F_SETTLE
	  || (dd->reverse_top->bConstr && ftype == F_CONSTR)) {
	nral = NRAL(ftype);
	n = gmx_mtop_ftype_count(err_top_global,ftype);
	ndiff = cl[ftype] - n;
	if (ndiff != 0) {
	  sprintf(buf,"%20s of %6d missing %6d",
		  interaction_function[ftype].longname,n,-ndiff);
	  fprintf(fplog,"%s\n",buf);
	  fprintf(stderr,"%s\n",buf);
	}
	rest_global -= n;
	rest_local  -= cl[ftype];
      }
    }
    
    ndiff = rest_local - rest_global;
    if (ndiff != 0) {
      sprintf(buf,"%20s of %6d missing %6d","exclusions",rest_global,-ndiff);
      fprintf(fplog,"%s\n",buf);
      fprintf(stderr,"%s\n",buf);
    }
  }

#ifdef HAVE_TO_UPDATE
  print_missing_interaction_atoms(fplog,cr,err_top_global,
				  &err_top_local->idef);
#endif

  if (DDMASTER(dd)) {
    if (ndiff_tot > 0) {
      gmx_incons("One or more interactions were multiple assigned in the domain decompostion");
    } else {
      gmx_fatal(FARGS,"%d of the %d bonded interactions could not be calculated because some atoms involved moved further apart than the multi-body cut-off distance (%g nm) (option -rdd) or the non-bonded cut-off distance (%g nm), for pairs and tabulated bonds see also option -ddbc",-ndiff_tot,cr->dd->nbonded_global,dd_cutoff_mbody(cr->dd),dd_cutoff(cr->dd));
    }
  }
}

static void global_atomnr_to_moltype_ind(gmx_molblock_ind_t *mbi,int i_gl,
					 int *mb,int *mt,int *mol,int *i_mol)
{
  int molb;

  *mb = 0;
  while (i_gl >= mbi->a_end) {
    (*mb)++;
    mbi++;
  }

  *mt    = mbi->type;
  *mol   = (i_gl - mbi->a_start) / mbi->natoms_mol;
  *i_mol = (i_gl - mbi->a_start) - (*mol)*mbi->natoms_mol;
}

static int count_excls(t_block *cgs,t_blocka *excls,int *n_intercg_excl)
{
  int n,n_inter,cg,at0,at1,at,excl,atj;
  
  n = 0;
  *n_intercg_excl = 0;
  for(cg=0; cg<cgs->nr; cg++) {
    at0 = cgs->index[cg];
    at1 = cgs->index[cg+1];
    for(at=at0; at<at1; at++) {
      for(excl=excls->index[at]; excl<excls->index[at+1]; excl++) {
	atj = excls->a[excl];
	if (atj > at) {
	  n++;
	  if (atj < at0 || atj >= at1)
	    (*n_intercg_excl)++;
	}
      }
    }
  }  
  
  return n;
}

static int make_reverse_moltype(t_ilist *il_mt,t_atom *atom,
				int **vsite_pbc,
				int *count,
				gmx_reverse_top_t *rt,
				int *r_index,int *r_il,
				bool bAssign)
{
  int  ftype,nral,i,j;
  t_ilist *il;
  t_iatom *ia;
  atom_id a;
  int  nint;

  nint = 0;
  for(ftype=0; ftype<F_NRE; ftype++) {
    if ((interaction_function[ftype].flags & (IF_BOND | IF_VSITE))
	|| ftype == F_SETTLE || (rt->bConstr && ftype == F_CONSTR)) {
      nral = NRAL(ftype);
      il = &il_mt[ftype];
      ia  = il->iatoms;
      for(i=0; i<il->nr; i+=1+nral) {
	ia = il->iatoms + i;
	/* Couple to the first atom in the interaction */
	a = ia[1];
	if (bAssign) {
	  r_il[r_index[a]+count[a]] = ftype;
	  r_il[r_index[a]+count[a]+1] = ia[0];
	  for(j=1; j<1+nral; j++) {
	    /* Store the molecular atom number */
	    r_il[r_index[a]+count[a]+1+j] = ia[j];
	  }
	}
	if (interaction_function[ftype].flags & IF_VSITE) {
	  if (bAssign) {
	    /* Add an entry to iatoms for storing 
	     * which of the constructing atoms are vsites again.
	     */
	    r_il[r_index[a]+count[a]+2+nral] = 0;
	    for(j=2; j<1+nral; j++) {
	      if (atom[ia[j]].ptype == eptVSite) {
		r_il[r_index[a]+count[a]+2+nral] |= (2<<j);
	      }
	    }
	    /* Store the vsite pbc atom in a second extra entry */
	    r_il[r_index[a]+count[a]+2+nral+1] =
	      (vsite_pbc ? vsite_pbc[ftype-F_VSITE2][i/(1+nral)] : -2);
	  }
	} else {
	  /* We do not count vsites since they are always uniquely assigned
	   * and can be assigned to multiple nodes with recursive vsites.
	   */
	  if (rt->bBCheck ||
	      !(interaction_function[ftype].flags & IF_LIMZERO)) {
	    nint++;
	  }
	}
	count[a] += 2 + nral_rt(ftype);
      }
    }
  }

  return nint;
}

static gmx_reverse_top_t *make_reverse_top(gmx_mtop_t *mtop,bool bFE,
					   int ***vsite_pbc_molt,
					   bool bConstr,
					   bool bBCheck,int *nint)
{
  int mt,nat_mt,*count,i,mb;
  gmx_reverse_top_t *rt;
  int *nint_mt;
  gmx_moltype_t *molt;
  gmx_reverse_ilist_t *ril_mt;

  snew(rt,1);

  /* Should we include constraints (for SHAKE) in rt? */
  rt->bConstr = bConstr;
  rt->bBCheck = bBCheck;

  rt->bMultiCGmols = FALSE;
  snew(nint_mt,mtop->nmoltype);
  snew(rt->ril_mt,mtop->nmoltype);
  rt->ril_mt_tot_size = 0;
  for(mt=0; mt<mtop->nmoltype; mt++) {
    molt = &mtop->moltype[mt];
    nat_mt = molt->atoms.nr;
    if (molt->cgs.nr > 1) {
      rt->bMultiCGmols = TRUE;
    }
    /* Count the interactions */
    snew(count,nat_mt);
    make_reverse_moltype(molt->ilist,molt->atoms.atom,
			 vsite_pbc_molt ? vsite_pbc_molt[mt] : NULL,
			 count,
			 rt,NULL,NULL,FALSE);

    ril_mt = &rt->ril_mt[mt];
    snew(ril_mt->index,nat_mt+1);
    ril_mt->index[0] = 0;
    for(i=0; i<nat_mt; i++) {
      ril_mt->index[i+1] = ril_mt->index[i] + count[i];
      count[i] = 0;
    }
    snew(ril_mt->il,ril_mt->index[nat_mt]);
    
    /* Store the interactions */
    nint_mt[mt] = make_reverse_moltype(molt->ilist,molt->atoms.atom,
				       vsite_pbc_molt ? vsite_pbc_molt[mt] : NULL,
				       count,
				       rt,ril_mt->index,ril_mt->il,TRUE);
    
    sfree(count);

    rt->ril_mt_tot_size += ril_mt->index[nat_mt];
  }
  if (debug) {
    fprintf(debug,"The total size of the atom to interaction index is %d integers\n",rt->ril_mt_tot_size);
  }

  *nint = 0;
  for(mb=0; mb<mtop->nmolblock; mb++) {
    *nint += mtop->molblock[mb].nmol*nint_mt[mtop->molblock[mb].type];
  }
  sfree(nint_mt);

  if (bFE && gmx_mtop_bondeds_free_energy(mtop)) {
    rt->ilsort = ilsortFE_UNSORTED;
  } else {
    rt->ilsort = ilsortNO_FE;
  }

  /* Make a molblock index for fast searching */
  snew(rt->mbi,mtop->nmolblock);
  i = 0;
  for(mb=0; mb<mtop->nmolblock; mb++) {
    rt->mbi[mb].a_start    = i;
    i += mtop->molblock[mb].nmol*mtop->molblock[mb].natoms_mol;
    rt->mbi[mb].a_end      = i;
    rt->mbi[mb].natoms_mol = mtop->molblock[mb].natoms_mol;
    rt->mbi[mb].type       = mtop->molblock[mb].type;
  }

  return rt;
}

void dd_make_reverse_top(FILE *fplog,
			 gmx_domdec_t *dd,gmx_mtop_t *mtop,
			 gmx_vsite_t *vsite,gmx_constr_t constr,
			 t_inputrec *ir,bool bBCheck)
{
  int mb,natoms,n_recursive_vsite,nexcl,nexcl_icg,a;
  gmx_molblock_t *molb;
  gmx_moltype_t *molt;

  if (fplog)
    fprintf(fplog,"\nLinking all bonded interactions to atoms\n");

  dd->reverse_top = make_reverse_top(mtop,ir->efep!=efepNO,
				     vsite ? vsite->vsite_pbc_molt : NULL,
				     !dd->bInterCGcons,
				     bBCheck,&dd->nbonded_global);

  nexcl = 0;
  dd->n_intercg_excl = 0;
  for(mb=0; mb<mtop->nmolblock; mb++) {
    molb = &mtop->molblock[mb];
    molt = &mtop->moltype[molb->type];
    nexcl += molb->nmol*count_excls(&molt->cgs,&molt->excls,&nexcl_icg);
    dd->n_intercg_excl += molb->nmol*nexcl_icg;
  }
  if (EEL_EXCL_FORCES(ir->coulombtype)) {
    dd->nbonded_global += nexcl;
    if (dd->n_intercg_excl && fplog)
      fprintf(fplog,"There are %d inter charge-group exclusions,\n"
	      "will use an extra communication step for exclusion forces for %s\n",
	      dd->n_intercg_excl,eel_names[ir->coulombtype]);
  }
  
  natoms = mtop->natoms;
  snew(dd->ga2la,natoms);
  for(a=0; a<natoms; a++)
    dd->ga2la[a].cell = -1;
  
  if (vsite && vsite->n_intercg_vsite > 0) {
    if (fplog)
      fprintf(fplog,"There are %d inter charge-group virtual sites,\n"
	      "will an extra communication step for selected coordinates and forces\n",
	      vsite->n_intercg_vsite);
    init_domdec_vsites(dd,natoms);
  }

  if (dd->bInterCGcons) {
    init_domdec_constraints(dd,natoms,mtop,constr);
  }
  if (fplog)
    fprintf(fplog,"\n");
}

static inline void add_ifunc(int nral,t_iatom *tiatoms,t_ilist *il)
{
  t_iatom *liatoms;
  int     k;

  if (il->nr+1+nral > il->nalloc) {
    il->nalloc += over_alloc_large(il->nr+1+nral);
    srenew(il->iatoms,il->nalloc);
  }
  liatoms = il->iatoms + il->nr;
  for(k=0; k<=nral; k++)
    liatoms[k] = tiatoms[k];
  il->nr += 1 + nral;
}

static void add_posres(int mol,int a_mol,gmx_molblock_t *molb,
		       t_iatom *iatoms,t_idef *idef)
{
  int n,a_molb;
  t_iparams *ip;
  
  /* This position restraint has not been added yet,
   * so it's index is the current number of position restraints.
   */
  n = idef->il[F_POSRES].nr/2;
  if (n >= idef->iparams_posres_nalloc) {
    idef->iparams_posres_nalloc = over_alloc_dd(n);
    srenew(idef->iparams_posres,idef->iparams_posres_nalloc);
  }
  ip = &idef->iparams_posres[n];
  /* Copy the force constants */
  *ip = idef->iparams[iatoms[0]];

  /* Get the position restriant coordinats from the molblock */
  a_molb = mol*molb->natoms_mol + a_mol;
  if (a_molb >= molb->nposres_xA) {
    gmx_incons("Not enough position restraint coordinates");
  }
  ip->posres.pos0A[XX] = molb->posres_xA[a_molb][XX];
  ip->posres.pos0A[YY] = molb->posres_xA[a_molb][YY];
  ip->posres.pos0A[ZZ] = molb->posres_xA[a_molb][ZZ];
  if (molb->nposres_xB > 0) {
    ip->posres.pos0B[XX] = molb->posres_xB[a_molb][XX];
    ip->posres.pos0B[YY] = molb->posres_xB[a_molb][YY];
    ip->posres.pos0B[ZZ] = molb->posres_xB[a_molb][ZZ];
  } else {
    ip->posres.pos0B[XX] = ip->posres.pos0A[XX];
    ip->posres.pos0B[YY] = ip->posres.pos0A[YY];
    ip->posres.pos0B[ZZ] = ip->posres.pos0A[ZZ];
  }
  /* Set the parameter index for idef->iparams_posre */
  iatoms[0] = n;
}

static void add_vsite(gmx_domdec_t *dd,int *index,int *rtil,
		      int ftype,int nral,
		      bool bHomeA,int a,int a_gl,int a_mol,
		      t_iatom *iatoms,
		      t_idef *idef,int **vsite_pbc,int *vsite_pbc_nalloc)
{
  int  k,ak_gl,vsi,pbc_a_mol;
  t_iatom tiatoms[1+MAXATOMLIST],*iatoms_r;
  gmx_ga2la_t *ga2la;
  int  j,ftype_r,nral_r;
  
  /* Copy the type */
  tiatoms[0] = iatoms[0];

  if (bHomeA) {
    /* We know the local index of the first atom */
    tiatoms[1] = a;
  } else {
    /* Convert later in make_local_vsites */
    tiatoms[1] = -a_gl - 1;
  }

  for(k=2; k<1+nral; k++) {
    ak_gl = a_gl + iatoms[k] - a_mol;
    ga2la = &dd->ga2la[ak_gl];
    if (ga2la->cell == 0) {
      tiatoms[k] = ga2la->a;
    } else {
      /* Copy the global index, convert later in make_local_vsites */
      tiatoms[k] = -(ak_gl + 1);
    }
  }

  /* Add this interaction to the local topology */
  add_ifunc(nral,tiatoms,&idef->il[ftype]);
  if (vsite_pbc) {
    vsi = idef->il[ftype].nr/(1+nral) - 1;
    if (vsi >= vsite_pbc_nalloc[ftype-F_VSITE2]) {
      vsite_pbc_nalloc[ftype-F_VSITE2] = over_alloc_large(vsi+1);
      srenew(vsite_pbc[ftype-F_VSITE2],vsite_pbc_nalloc[ftype-F_VSITE2]);
    }
    if (bHomeA) {
      pbc_a_mol = iatoms[1+nral+1];
      if (pbc_a_mol < 0) {
	/* The pbc flag is one of the following two options:
	 * -2: vsite and all constructing atoms are within the same cg, no pbc
	 * -1: vsite and its first constructing atom are in the same cg, do pbc
	 */
	vsite_pbc[ftype-F_VSITE2][vsi] = pbc_a_mol;
      } else {
	/* Set the pbc atom for this vsite so we can make its pbc 
	 * identical to the rest of the atoms in its charge group.
	 * Since the order of the atoms does not change within a charge group,
	 * we do no need to access to global to local atom index.
	 */
	vsite_pbc[ftype-F_VSITE2][vsi] = a + pbc_a_mol - iatoms[1];
      }
    } else {
      /* This vsite is non-home (required for recursion),
       * and therefore there is no charge group to match pbc with.
       * But we always turn on full_pbc to assure that higher order
       * recursion works correctly.
       */
      vsite_pbc[ftype-F_VSITE2][vsi] = -1;
    }
  }
  
  if (iatoms[1+nral]) {
    /* Check for recursion */
    for(k=2; k<1+nral; k++) {
      if ((iatoms[1+nral] & (2<<k)) && (tiatoms[k] < 0)) {
	/* This construction atoms is a vsite and not a home atom */
	if (gmx_debug_at)
	  fprintf(debug,"Constructing atom %d of vsite atom %d is a vsite and non-home\n",iatoms[k]+1,a_mol+1);

	/* Find the vsite construction */

	/* Check all interactions assigned to this atom */
	j = index[iatoms[k]];
	while (j < index[iatoms[k]+1]) {
	  ftype_r = rtil[j++];
	  nral_r = NRAL(ftype_r);
	  if (interaction_function[ftype_r].flags & IF_VSITE) {
	    /* Add this vsite (recursion) */
	    add_vsite(dd,index,rtil,ftype_r,nral_r,
		      FALSE,-1,a_gl+iatoms[k]-iatoms[1],iatoms[k],
		      rtil+j,idef,vsite_pbc,vsite_pbc_nalloc);
	    j += 1 + nral_r + 2;
	  } else {
	    j += 1 + nral_r;
	  }
	}
      }
    }
  }
}

static void make_la2lc(gmx_domdec_t *dd)
{
  int *cgindex,*la2lc,cg,a;

  cgindex = dd->cgindex;

  if (dd->nat_tot > dd->la2lc_nalloc) {
    dd->la2lc_nalloc = over_alloc_dd(dd->nat_tot);
    snew(dd->la2lc,dd->la2lc_nalloc);
  }
  la2lc = dd->la2lc;

  /* Make the local atom to local cg index */
  for(cg=0; cg<dd->ncg_tot; cg++) {
    for(a=cgindex[cg]; a<cgindex[cg+1]; a++) {
      la2lc[a] = cg;
    }
  }
}

static real dd_dist2(t_pbc *pbc_null,rvec *cg_cm,const int *la2lc,int i,int j)
{
  rvec dx;

  if (pbc_null) {
    pbc_dx_aiuc(pbc_null,cg_cm[la2lc[i]],cg_cm[la2lc[j]],dx);
  } else {
    rvec_sub(cg_cm[la2lc[i]],cg_cm[la2lc[j]],dx);
  }
  
  return norm2(dx);
}

static int make_local_bondeds(gmx_domdec_t *dd,gmx_molblock_t *molb,
			      bool bRCheckMB,ivec rcheck,bool bRCheck2B,
			      real rc,
			      int *la2lc,t_pbc *pbc_null,rvec *cg_cm,
			      t_idef *idef,gmx_vsite_t *vsite)
{
  int ncell,nicell,ic,la0,la1,i,i_gl,mb,mt,mol,i_mol,j,ftype,nral,d,k,kc;
  int *index,*rtil,**vsite_pbc,*vsite_pbc_nalloc;
  t_iatom *iatoms,tiatoms[1+MAXATOMLIST];
  bool bBCheck,bUse;
  real rc2;
  ivec k_zero,k_plus;
  gmx_ga2la_t *ga2la;
  gmx_domdec_ns_ranges_t *icell;
  gmx_reverse_top_t *rt;
  gmx_molblock_ind_t *mbi;
  int nbonded_local;
  
  ncell  = dd->ncell;
  nicell = dd->nicell;
  icell  = dd->icell;

  rc2 = rc*rc;

  if (vsite && vsite->n_intercg_vsite > 0) {
    vsite_pbc        = vsite->vsite_pbc_loc;
    vsite_pbc_nalloc = vsite->vsite_pbc_loc_nalloc;
  } else {
    vsite_pbc        = NULL;
    vsite_pbc_nalloc = NULL;
  }

  rt = dd->reverse_top;

  bBCheck = rt->bBCheck;

  /* Clear the counts */
  for(ftype=0; ftype<F_NRE; ftype++) {
    idef->il[ftype].nr = 0;
  }
  nbonded_local = 0;

  mbi = rt->mbi;
  
  for(ic=0; ic<ncell; ic++) {
    la0 = dd->cgindex[dd->ncg_cell[ic]];
    la1 = dd->cgindex[dd->ncg_cell[ic+1]];
    for(i=la0; i<la1; i++) {
      /* Get the global atom number */
      i_gl = dd->gatindex[i];
      global_atomnr_to_moltype_ind(mbi,i_gl,&mb,&mt,&mol,&i_mol);
      /* Check all interactions assigned to this atom */
      index = rt->ril_mt[mt].index;
      rtil  = rt->ril_mt[mt].il;
      j = index[i_mol];
      while (j < index[i_mol+1]) {
	ftype  = rtil[j++];
	iatoms = rtil + j;
	nral = NRAL(ftype);
	if (interaction_function[ftype].flags & IF_VSITE) {
	  /* The vsite construction goes where the vsite itself is */
	  if (ic == 0)
	    add_vsite(dd,index,rtil,ftype,nral,
		      TRUE,i,i_gl,i_mol,
		      iatoms,idef,vsite_pbc,vsite_pbc_nalloc);
	  j += 1 + nral + 2;
	} else {
	  /* Copy the type */
	  tiatoms[0] = iatoms[0];

	  if (nral == 1) {
	    /* Assign single-body interactions to the home cell */
	    if (ic == 0) {
	      bUse = TRUE;
	      tiatoms[1] = i;
	      if (ftype == F_POSRES) {
		add_posres(mol,i_mol,&molb[mb],tiatoms,idef);
	      }
	    } else {
	      bUse = FALSE;
	    }
	  } else if (nral == 2) {
	    /* This is a two-body interaction,
	     * we can assign analogous to the non-bonded assignments.
	     */
	    ga2la = &dd->ga2la[i_gl + iatoms[2] - i_mol];
	    kc = ga2la->cell;
	    if (kc == -1) {
	      bUse = FALSE;
	    } else {
	      if (kc >= ncell)
		kc -= ncell;
	      /* Check zone interaction assignments */
	      bUse = ((ic < nicell && ic <= kc &&
		       icell[ic].j0 <= kc && kc < icell[ic].j1) ||
		      (kc < nicell && ic >  kc &&
		       icell[kc].j0 <= ic && ic < icell[kc].j1));
	      if (bUse) {
		tiatoms[1] = i;
		tiatoms[2] = ga2la->a;
		/* If necessary check the cgcm distance */
		if (bRCheck2B && dd_dist2(pbc_null,cg_cm,la2lc,
					  tiatoms[1],tiatoms[2]) >= rc2) {
		  bUse = FALSE;
		}
	      }
	    }
	  } else {
	    /* Assign this multi-body bonded interaction to the local node
	     * if we have all the atoms involved (local or communicated)
	     * and the minimum cell shift in each dimension is zero,
	     * for dimensions with 2 DD cell an extra check may be necessary.
	     */
	    bUse = TRUE;
	    clear_ivec(k_zero);
	    clear_ivec(k_plus);
	    for(k=1; k<=nral && bUse; k++) {
	      ga2la = &dd->ga2la[i_gl + iatoms[k] - i_mol];
	      kc = ga2la->cell;
	      if (kc == -1 || kc >= dd->ncell) {
		/* We do not have this atom of this interaction locally,
		 * or it comes from more than one cell away.
		 */
		bUse = FALSE;
	      } else {
		tiatoms[k] = ga2la->a;
		for(d=0; d<DIM; d++) {
		  if (dd->shift[kc][d] == 0)
		    k_zero[d] = k;
		  else
		    k_plus[d] = k;
		}
	      }
	    }
	    bUse = (bUse && k_zero[XX] && k_zero[YY] && k_zero[ZZ]);
	    if (bRCheckMB) {
	      for(d=0; (d<DIM && bUse); d++) {
		/* Check if the cg_cm distance falls within the cut-off
		 * to avoid possible multiple assignments of bonded interactions.
		 */
		if (rcheck[d] && 
		    k_plus[d] &&
		    dd_dist2(pbc_null,cg_cm,la2lc,
			     tiatoms[k_zero[d]],tiatoms[k_plus[d]]) >= rc2)
		  bUse = FALSE;
	      }
	    }
	  }
	  if (bUse) {
	    /* Add this interaction to the local topology */
	    add_ifunc(nral,tiatoms,&idef->il[ftype]);
	    /* Sum so we can check in global_stat if we have everything */
	    if (bBCheck || !(interaction_function[ftype].flags & IF_LIMZERO)) {
	      nbonded_local++;
	    }
	  }
	  j += 1 + nral;
	}
      }
    }
  }

  return nbonded_local;
}

static int make_local_bondeds_intracg(gmx_domdec_t *dd,gmx_molblock_t *molb,
				      t_idef *idef,gmx_vsite_t *vsite)
{
  int i,i_gl,mb,mt,mol,i_mol,j,ftype,nral,k;
  int *index,*rtil,**vsite_pbc,*vsite_pbc_nalloc;
  t_iatom *iatoms,tiatoms[1+MAXATOMLIST];
  gmx_reverse_top_t *rt;
  gmx_molblock_ind_t *mbi;
  int nbonded_local;

  if (vsite && vsite->n_intercg_vsite > 0) {
    vsite_pbc        = vsite->vsite_pbc_loc;
    vsite_pbc_nalloc = vsite->vsite_pbc_loc_nalloc;
  } else {
    vsite_pbc        = NULL;
    vsite_pbc_nalloc = NULL;
  }

  /* Clear the counts */
  for(ftype=0; ftype<F_NRE; ftype++) {
    idef->il[ftype].nr = 0;
  }
  nbonded_local = 0;

  rt = dd->reverse_top;

  if (rt->ril_mt_tot_size == 0) {
    /* There are no interactions to assign */
    return nbonded_local;
  }

  mbi = rt->mbi;

  for(i=0; i<dd->nat_home; i++) {
    /* Get the global atom number */
    i_gl = dd->gatindex[i];
    global_atomnr_to_moltype_ind(mbi,i_gl,&mb,&mt,&mol,&i_mol);
    /* Check all interactions assigned to this atom */
    index = rt->ril_mt[mt].index;
    rtil  = rt->ril_mt[mt].il;
    /* Check all interactions assigned to this atom */
    j = index[i_mol];
    while (j < index[i_mol+1]) {
      ftype  = rtil[j++];
      iatoms = rtil + j;
      nral = NRAL(ftype);
      if (interaction_function[ftype].flags & IF_VSITE) {
	/* The vsite construction goes where the vsite itself is */
	add_vsite(dd,index,rtil,ftype,nral,
		  TRUE,i,i_gl,i_mol,
		  iatoms,idef,vsite_pbc,vsite_pbc_nalloc);
	j += 1 + nral + 2;
      } else {
	/* Copy the type */
	tiatoms[0] = iatoms[0];
	tiatoms[1] = i;
	for(k=2; k<=nral; k++) {
	  tiatoms[k] = i + iatoms[k] - iatoms[1];
	}
	if (ftype == F_POSRES) {
	  add_posres(mol,i_mol,&molb[mb],tiatoms,idef);
	}
	/* Add this interaction to the local topology */
	add_ifunc(nral,tiatoms,&idef->il[ftype]);
	/* Sum so we can check in global_stat if we have everything */
	nbonded_local++;
	j += 1 + nral;
      }
    }
  }

  return nbonded_local;
}

static int make_local_exclusions(gmx_domdec_t *dd,gmx_mtop_t *mtop,
				 bool bRCheck,real rc,
				 int *la2lc,t_pbc *pbc_null,rvec *cg_cm,
				 t_forcerec *fr,
				 t_blocka *lexcls)
{
  int  nicell,n,count,ic,jla0,jla1,jla;
  int  cg,la0,la1,la,a_gl,mb,mt,mol,a_mol,j,aj_mol;
  t_blocka *excls;
  gmx_ga2la_t *ga2la;
  gmx_molblock_ind_t *mbi;
  real rc2;

  /* Since for RF and PME we need to loop over the exclusions
   * we should store each exclusion only once. This is done
   * using the same cell scheme as used for neighbor searching.
   * The exclusions involving non-home atoms are stored only
   * one way: atom j is in the excl list of i only for j > i,
   * where i and j are local atom numbers.
   */

  lexcls->nr = dd->cgindex[dd->icell[dd->nicell-1].cg1];
  if (lexcls->nr+1 > lexcls->nalloc_index) {
    lexcls->nalloc_index = over_alloc_dd(lexcls->nr)+1;
    srenew(lexcls->index,lexcls->nalloc_index);
  }

  mbi = dd->reverse_top->mbi;
  
  rc2 = rc*rc;

  if (dd->n_intercg_excl)
    nicell = dd->nicell;
  else
    nicell = 1;
  n = 0;
  count = 0;
  for(ic=0; ic<nicell; ic++) {
    jla0 = dd->cgindex[dd->icell[ic].jcg0];
    jla1 = dd->cgindex[dd->icell[ic].jcg1];
    for(cg=dd->ncg_cell[ic]; cg<dd->ncg_cell[ic+1]; cg++) {
      /* Here we assume the number of exclusions in one charge group
       * is never larger than 1000.
       */
      if (n+1000 > lexcls->nalloc_a) {
	lexcls->nalloc_a = over_alloc_large(n+1000);
	srenew(lexcls->a,lexcls->nalloc_a);
      }
      la0 = dd->cgindex[cg];
      la1 = dd->cgindex[cg+1];
      if (GET_CGINFO_EXCL_INTER(fr->cginfo[cg]) ||
	  !GET_CGINFO_EXCL_INTRA(fr->cginfo[cg])) {
	/* Copy the exclusions from the global top */
	for(la=la0; la<la1; la++) {
	  lexcls->index[la] = n;
	  a_gl = dd->gatindex[la];
	  global_atomnr_to_moltype_ind(mbi,a_gl,&mb,&mt,&mol,&a_mol);
	  excls = &mtop->moltype[mt].excls;
	  for(j=excls->index[a_mol]; j<excls->index[a_mol+1]; j++) {
	    aj_mol = excls->a[j];
	    /* This computation of jla is only correct if intra-cg */
	    jla = la + aj_mol - a_mol;
	    if (jla >= la0 && jla < la1) {
	      /* This is an intra-cg exclusion.
	       * We can skip the global indexing and distance checking.
	       */
	      /* Intra-cg exclusions are only required for the home cell */
	      if (ic == 0) {
		lexcls->a[n++] = jla;
		/* Check to avoid double counts */
		if (jla > la)
		  count++;
	      }
	    } else {
	      /* This is a inter-cg exclusion */
	      ga2la = &dd->ga2la[a_gl + aj_mol - a_mol];
	      /* Since exclusions are pair interactions,
	       * just like non-bonded interactions,
	       * they can be assigned properly up to the DD cutoff
	       * (not cutoff_min as for the other bonded interactions).
	       */
	      if (ga2la->cell != -1) {
		jla = ga2la->a;
		if (ic == 0 && ga2la->cell == 0) {
		  lexcls->a[n++] = jla;
		  /* Check to avoid double counts */
		  if (jla > la)
		    count++;
		} else if (jla >= jla0 && jla < jla1 &&
			   (!bRCheck ||
			    dd_dist2(pbc_null,cg_cm,la2lc,la,jla) < rc2)) {
		  /* jla > la, since jla0 > la */
		  lexcls->a[n++] = jla;
		  count++;
		}
	      }
	    }
	  }
	}
      } else {
	/* There are no inter-cg exclusions and this cg is self-excluded.
	 * These exclusions are only required for cell 0,
	 * since other cells do not see themselves.
	 */
	if (ic == 0) {
	  for(la=la0; la<la1; la++) {
	    lexcls->index[la] = n;
	    for(j=la0; j<la1; j++)
	      lexcls->a[n++] = j;
	  }
	  count += ((la1 - la0)*(la1 - la0 - 1))/2;
	} else {
	  /* We don't need exclusions for this cg */
	  for(la=la0; la<la1; la++) {
	    lexcls->index[la] = n;
	  }
	}
      }
    }
  }
  if (dd->n_intercg_excl == 0) {
    /* There are no exclusions involving non-home charge groups,
     * but we need to set the indices for neighborsearching.
     */
    la0 = dd->cgindex[dd->icell[0].cg1];
    for(la=la0; la<lexcls->nr; la++)
      lexcls->index[la] = n;
  }
  lexcls->index[lexcls->nr] = n;
  lexcls->nra = n;
  if (dd->n_intercg_excl == 0) {
    /* nr is only used to loop over the exclusions for Ewald and RF,
     * so we can set it to the number of home atoms for efficiency.
     */
    lexcls->nr = dd->cgindex[dd->icell[0].cg1];
  }
  if (debug)
    fprintf(debug,"We have %d exclusions, check count %d\n",
	    lexcls->nra,count);

  return count;
}

void dd_make_local_cgs(gmx_domdec_t *dd,t_block *lcgs)
{
  lcgs->nr    = dd->ncg_tot;
  lcgs->index = dd->cgindex;
}

void dd_make_local_top(FILE *fplog,gmx_domdec_t *dd,
		       matrix box,real rc,rvec cellsize_min,ivec npulse,
		       t_forcerec *fr,gmx_vsite_t *vsite,
		       gmx_mtop_t *mtop,t_topology *ltop)
{
  bool bUniqueExcl,bRCheckMB,bRCheck2B,bRCheckExcl;
  ivec rcheck;
  int  d,nexcl;
  t_pbc pbc,*pbc_null=NULL;

  if (debug)
    fprintf(debug,"Making local topology\n");

  ltop->name  = mtop->name;
  dd_make_local_cgs(dd,&ltop->cgs);

  bRCheckMB   = FALSE;
  bRCheck2B   = FALSE;
  bRCheckExcl = FALSE;
  
  if (!dd->reverse_top->bMultiCGmols) {
    /* We don't need any checks, assign all interactions with local atoms */

    dd->nbonded_local = make_local_bondeds_intracg(dd,mtop->molblock,
						   &ltop->idef,vsite);
  } else {
    /* We need to check to which cell bondeds should be assigned */
    
    /* Should we check cg_cm distances when assigning bonded interactions? */
    for(d=0; d<DIM; d++) {
      rcheck[d] = FALSE;
      /* Only need to check for dimensions where the part of the box
       * that is not communicated is smaller than the cut-off.
       */
      if (dd->nc[d] > 1 && (dd->nc[d] - npulse[d])*cellsize_min[d] < 2*rc) {
	if (dd->nc[d] == 2) {
	  rcheck[d] = TRUE;
	  bRCheckMB = TRUE;
	}
	/* Check for interactions between two atoms,
	 * where we can allow interactions up to the cut-off,
	 * instead of up to the smallest cell dimension.
	 */
	bRCheck2B = TRUE;
	if (debug)
	  fprintf(debug,"bonded rcheck[%d] = %d, bRCheck2B = %d\n",
		  d,rcheck[d],bRCheck2B);
      }
    }
    if (EEL_EXCL_FORCES(fr->eeltype)) {
      bRCheckExcl = bRCheck2B;
    } else {
      /* If we don't have forces on exclusions,
       * we don't care about exclusions being assigned mulitple times.
       */
      bRCheckExcl = FALSE;
    }
    if (bRCheckMB || bRCheck2B) {
      make_la2lc(dd);
      if (fr->bMolPBC) {
	set_pbc_dd(&pbc,fr->ePBC,dd,TRUE,box);
	pbc_null = &pbc;
      } else {
	pbc_null = NULL;
      }
    }

    dd->nbonded_local = make_local_bondeds(dd,mtop->molblock,
					   bRCheckMB,rcheck,bRCheck2B,rc,
					   dd->la2lc,
					   pbc_null,fr->cg_cm,
					   &ltop->idef,vsite);
  }

  if (dd->reverse_top->ilsort == ilsortNO_FE) {
    ltop->idef.ilsort = ilsortNO_FE;
  } else {
    gmx_sort_ilist_fe(&ltop->idef);
  }

  nexcl = make_local_exclusions(dd,mtop,bRCheckExcl,
				rc,dd->la2lc,pbc_null,fr->cg_cm,
				fr,&ltop->excls);

  if (EEL_EXCL_FORCES(fr->eeltype))
    dd->nbonded_local += nexcl;
  
  ltop->atoms.atom = NULL;
  ltop->atomtypes  = mtop->atomtypes;
  ltop->symtab     = mtop->symtab;

  /* For an error message only */
  err_top_global = mtop;
  err_top_local  = ltop;
}

t_topology *dd_init_local_top(gmx_mtop_t *top_global)
{
  t_topology *top;
  int i;
  
  snew(top,1);

  top->idef.ntypes   = top_global->ffparams.ntypes;
  top->idef.atnr     = top_global->ffparams.atnr;
  top->idef.functype = top_global->ffparams.functype;
  top->idef.iparams  = top_global->ffparams.iparams;
  top->idef.fudgeQQ  = top_global->ffparams.fudgeQQ;

  for(i=0; i<F_NRE; i++) {
    top->idef.il[i].iatoms = NULL;
    top->idef.il[i].nalloc = 0;
  }
  top->idef.ilsort   = ilsortUNKNOWN;
  
  top->atoms.nr   = top_global->natoms;
  top->atoms.atom = 0;

  return top;
}

void dd_init_local_state(gmx_domdec_t *dd,
			 t_state *state_global,t_state *state_local)
{
  int buf[2];

  if (DDMASTER(dd)) {
    buf[0] = state_global->flags;
    buf[1] = state_global->ngtc;
  }
  dd_bcast(dd,2*sizeof(int),buf);

  init_state(state_local,0,buf[1]);
  state_local->flags = buf[0];

  /* With Langevin Dynamics we need to make proper storage space
   * in the global and local state for the random numbers.
   */
  if (state_local->flags & (1<<estLD_RNG)) {
    if (DDMASTER(dd) && state_global->nrngi > 1) {
      state_global->nrng = dd->nnodes*gmx_rng_n();
      srenew(state_global->ld_rng,state_global->nrng);
    }
    state_local->nrng = gmx_rng_n();
    snew(state_local->ld_rng,state_local->nrng);
  }
  if (state_local->flags & (1<<estLD_RNGI)) {
    if (DDMASTER(dd) && state_global->nrngi > 1) {
      srenew(state_global->ld_rngi,dd->nnodes);
    }
    snew(state_local->ld_rngi,1);
  }
}

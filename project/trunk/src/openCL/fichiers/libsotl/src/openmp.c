
#include "default_defines.h"
#include "global_definitions.h"
#include "device.h"
#include "openmp.h"
#include "sotl.h"

#ifdef HAVE_LIBGL
#include "vbo.h"
#endif

#include <stdio.h>
#include <stdlib.h>


static int *atom_state = NULL;

#ifdef HAVE_LIBGL

#define SHOCK_PERIOD  50

void first_touch(sotl_device_t *dev)
{
  sotl_atom_set_t *set = &dev->atom_set;
  sotl_domain_t *domain = &dev->domain;

  for (unsigned n = 0; n < set->natoms; n++) {
    vbo_vertex[n*3 + 0] += 0;
    vbo_vertex[n*3 + 1] += 0;
    vbo_vertex[n*3 + 2] += 0;

    set->pos.x[n]+=0;
    set->speed.dx[n]+=0;
    set->pos.y[n]+=0;
    set->speed.dy[n]+=0;
    set->pos.z[n]+=0;
    set->speed.dz[n]+=0;

    // Atom color depends on z coordinate
    
      float rate= domain->min_ext[2] / (domain->max_ext[2]);
      
      vbo_color[n*3 + 0] += 0;
      vbo_color[n*3 + 1] += 0;
      vbo_color[n*3 + 2] += 0;
      atom_state[n] =  atom_state[n] + rate - rate;
  }

}

// Update OpenGL Vertex Buffer Object
//
static void omp_update_vbo (sotl_device_t *dev)
{
  sotl_atom_set_t *set = &dev->atom_set;
  sotl_domain_t *domain = &dev->domain;

  #pragma omp parallel
  #pragma omp for schedule(runtime)
  for (unsigned n = 0; n < set->natoms; n++) {
    vbo_vertex[n*3 + 0] = set->pos.x[n];
    vbo_vertex[n*3 + 1] = set->pos.y[n];
    vbo_vertex[n*3 + 2] = set->pos.z[n];

    // Atom color depends on z coordinate
    {
      float ratio = (set->pos.z[n] - domain->min_ext[2]) / (domain->max_ext[2] - domain->min_ext[2]);

      vbo_color[n*3 + 0] = (1.0 - ratio) * atom_color[0].R + ratio * 1.0;
      vbo_color[n*3 + 1] = (1.0 - ratio) * atom_color[0].G + ratio * 0.0;
      vbo_color[n*3 + 2] = (1.0 - ratio) * atom_color[0].B + ratio * 0.0;
      atom_state[n]--;
    }
  }
}
#endif

// Update positions of atoms by adding (dx, dy, dz)
//
static void omp_move (sotl_device_t *dev)
{
  sotl_atom_set_t *set = &dev->atom_set;

  #pragma omp parallel 
  #pragma omp for schedule(runtime)
  for (unsigned n = 0; n < set->natoms; n++) {
    set->pos.x[n] += set->speed.dx[n];
    set->pos.y[n] += set->speed.dy[n];
    set->pos.z[n] += set->speed.dz[n];
  }
}

// Apply gravity force
//
static void omp_gravity (sotl_device_t *dev)
{
  sotl_atom_set_t *set = &dev->atom_set;
  const calc_t g = 0.005;
  #pragma omp parallel
  #pragma omp for schedule(runtime)
   for (unsigned current = 0; current < set->natoms; current++) {
	set->speed.dy[current] = set->speed.dy[current]-g;
  }
}

static void omp_bounce (sotl_device_t *dev)
{
  sotl_atom_set_t *set = &dev->atom_set;
  sotl_domain_t *domain = &dev->domain;
    #pragma omp parallel 
    #pragma omp for schedule(runtime)
     for (unsigned current = 0; current < set->natoms; current++) {
	if(set->pos.x[current] < domain->min_ext[0] && set->speed.dx[current]<0){
		set->speed.dx[current] = set->speed.dx[current]*(-1);
	}
	if(set->pos.x[current] > domain->max_ext[0] && set->speed.dx[current]>0){
		set->speed.dx[current] = set->speed.dx[current]*(-1);
	}
 	if(set->pos.y[current] < domain->min_ext[1] && set->speed.dy[current]<0){
		set->speed.dy[current] = set->speed.dy[current]*(-1);
	}	
	if(set->pos.y[current] > domain->max_ext[1] && set->speed.dy[current]>0){
		set->speed.dy[current] = set->speed.dy[current]*(-1);
	}
	if(set->pos.z[current] < domain->min_ext[2] && set->speed.dz[current]<0){
		set->speed.dz[current] = set->speed.dz[current]*(-1);
	}
	if(set->pos.z[current] > domain->max_ext[2] && set->speed.dz[current]>0){
		set->speed.dz[current] = set->speed.dz[current]*(-1);
	}
  }
}

static calc_t squared_distance (sotl_atom_set_t *set, unsigned p1, unsigned p2)
{
  calc_t *pos1 = set->pos.x + p1,
    *pos2 = set->pos.x + p2;

  calc_t dx = pos2[0] - pos1[0],
         dy = pos2[set->offset] - pos1[set->offset],
         dz = pos2[set->offset*2] - pos1[set->offset*2];

  return dx * dx + dy * dy + dz * dz;
}

static calc_t lennard_jones (calc_t r2)
{
  calc_t rr2 = 1.0 / r2;
  calc_t r6;

  r6 = LENNARD_SIGMA * LENNARD_SIGMA * rr2;
  r6 = r6 * r6 * r6;

  return 24 * LENNARD_EPSILON * rr2 * (2.0f * r6 * r6 - r6);
}

void echange(sotl_atom_set_t * set, int a, int b){
	calc_t tmpx;
	calc_t tmpy;	
	calc_t tmpz;
	calc_t tmpsx;
	calc_t tmpsy;
	calc_t tmpsz;

	tmpx = set->pos.x[a];
	tmpy = set->pos.y[a];
	tmpz = set->pos.z[a];
	tmpsx = set->speed.dx[a];
	tmpsy = set->speed.dy[a];
	tmpsz = set->speed.dz[a];

	set->pos.x[a] = set->pos.x[b];
	set->pos.y[a] = set->pos.y[b];
	set->pos.z[a] = set->pos.z[b];
	set->speed.dx[a] = set->speed.dx[b];
	set->speed.dy[a] = set->speed.dy[b];
	set->speed.dz[a] = set->speed.dz[b];

	set->pos.x[b] = tmpx;
	set->pos.y[b] = tmpy;
	set->pos.z[b] = tmpz;
	set->speed.dx[b] = tmpsx;
	set->speed.dy[b] = tmpsy;
	set->speed.dz[b] = tmpsz;

}

void tri(sotl_atom_set_t * set){

  int paire = (set->natoms%2==0)?1:0;
  int impaire = (set->natoms%2==0)?0:1;
  for(unsigned i=0; i<set->natoms; i++){
    if(i%2==0){
      #pragma omp parallel
      #pragma omp for schedule(runtime)
      for(unsigned j =0; j<set->natoms-impaire;j+=2){
	if(set->pos.z[j]<set->pos.z[j+1]){
	 echange(set,j,j+1);
	}
      }
    }else{
      #pragma omp parallel
      #pragma omp for schedule(runtime)
      for(unsigned j=1;j<(set->natoms-paire);j+=2){
	if(set->pos.z[j]<set->pos.z[j+1]){
	  echange(set,j,j+1);
      }
    }
  }
}
}



static void omp_force (sotl_device_t *dev)
{
  sotl_atom_set_t *set = &dev->atom_set;
  tri(set);

  #pragma omp parallel
  #pragma omp for schedule(runtime)
  for (int current = 0; current < (int)set->natoms; current++) {
    calc_t force[3] = { 0.0, 0.0, 0.0 };

    //atome Z supperieur
    for(int other = current-1;other>-1;other--){
	if(abs(set->pos.z[current]-set->pos.z[other]) > LENNARD_SQUARED_CUTOFF){
		break;
	}else{
		calc_t sq_dist = squared_distance (set, current, other);
		if (sq_dist < LENNARD_SQUARED_CUTOFF) {
	  		calc_t intensity = lennard_jones (sq_dist);

	  		force[0] += intensity * (set->pos.x[current] - set->pos.x[other]);
	  		force[1] += intensity * (set->pos.x[set->offset + current] -
						 set->pos.x[set->offset + other]);
	 		force[2] += intensity * (set->pos.x[set->offset * 2 + current] -
				   		 set->pos.x[set->offset * 2 + other]);
		}
	}
    }
    //atome Z inferieur
    for(int other = current+1;other<(int)set->natoms;other++){
	if(abs(set->pos.z[current]-set->pos.z[other]) > LENNARD_SQUARED_CUTOFF){
		break;
	}else{
		calc_t sq_dist = squared_distance (set, current, other);
		if (sq_dist < LENNARD_SQUARED_CUTOFF) {
	  		calc_t intensity = lennard_jones (sq_dist);

	  		force[0] += intensity * (set->pos.x[current] - set->pos.x[other]);
		  	force[1] += intensity * (set->pos.x[set->offset + current] -
				   		 set->pos.x[set->offset + other]);
			force[2] += intensity * (set->pos.x[set->offset * 2 + current] -
						 set->pos.x[set->offset * 2 + other]);
	}
	}
    }
    set->speed.dx[current] += force[0];
    set->speed.dx[set->offset + current] += force[1];
    set->speed.dx[set->offset * 2 + current] += force[2];
	
  }

}

void omp_force_cube(sotl_device_t *dev){
	
  sotl_atom_set_t *set = &dev->atom_set;
  sotl_domain_t *domain = &dev->domain;

  sotl_atom_set_t *in = malloc(sizeof(sotl_atom_set_t));
  atom_set_init(in,set->natoms,set->offset);

 
  for(int i=0; i<(int)set->natoms; i++){
    atom_set_add(in,set->pos.x[i],set->pos.y[i],set->pos.z[i],set->speed.dx[i],set->speed.dy[i],set->speed.dz[i]);
  }

  int NbCubes = (int) domain->total_boxes;

  int* boite= calloc(NbCubes,sizeof(int));
  int* boiten= calloc(NbCubes,sizeof(int));

  for(int i= 0; i < (int)in->natoms; i++){
    int numb =atom_get_num_box(domain,in->pos.x[i],in->pos.y[i],in->pos.z[i],LENNARD_SQUARED_CUTOFF);
    boite[numb]++;
    boiten[numb]++;
  }

  
  for (int i = 1; i < NbCubes; i++){
    boite[i] += boite[i-1];
  }
	
  for(int i = 0; i < (int)in->natoms; i++){
    int numb =atom_get_num_box(domain,in->pos.x[i],in->pos.y[i],in->pos.z[i],LENNARD_SQUARED_CUTOFF);
    int indice = boite[numb-1]+boiten[numb]-1;
    set->pos.x[indice]=in->pos.x[i];
    set->pos.y[indice]=in->pos.y[i];
    set->pos.z[indice]=in->pos.z[i];
    set->speed.dx[indice]=in->speed.dx[i];
    set->speed.dy[indice]=in->speed.dy[i];
    set->speed.dz[indice]=in->speed.dz[i];
    boiten[numb]--;	
  }
    #pragma omp parallel
    #pragma omp for schedule(runtime)
    for (int current = 0; current < (int)set->natoms; current++) {
      calc_t force[3] = { 0.0, 0.0, 0.0 };

      int bx =(int)((set->pos.x[current] - domain->min_border[0]) *LENNARD_SQUARED_CUTOFF);
      int by =(int)((set->pos.y[current] - domain->min_border[1]) *LENNARD_SQUARED_CUTOFF);
      int bz =(int)((set->pos.z[current] - domain->min_border[2]) *LENNARD_SQUARED_CUTOFF);
      
      for(int z =(bz==0)?bz:bz-1; z<= bz+1 && z<(int)domain->boxes[2]; z++){
	for(int y=(by==0)?by:by-1; y<= by+1 && y<(int)domain->boxes[1]; y++){
	  for(int x=(bx==0)?bx:bx-1;x<=bx+1 && x<(int)domain->boxes[0];x++){
            int numb = z * domain->boxes[0]* domain->boxes[1] + y * domain->boxes[0] + x;
	    for(int other =(numb==0)?boite[0]:boite[numb-1]; other<boite[numb]; other++){
	      if(current != other){
                calc_t sq_dist = squared_distance (set, current, other);

		if (sq_dist < LENNARD_SQUARED_CUTOFF) {
		  calc_t intensity = lennard_jones (sq_dist);
		  force[0] += intensity * (set->pos.x[current] - set->pos.x[other]);
		  force[1] += intensity * (set->pos.x[set->offset + current] -
					   set->pos.x[set->offset + other]);
		  force[2] += intensity * (set->pos.x[set->offset * 2 + current] -
					   set->pos.x[set->offset * 2 + other]);
		}
	      }
	    }
	  }
	}
      } 
	
	set->speed.dx[current] += force[0];
        set->speed.dx[set->offset + current] += force[1];
        set->speed.dx[set->offset * 2 + current] += force[2];
    }
    
    free(boiten);
    atom_set_free(in);
    free(in);
    free(boite);
  }

// Main simulation function
//
void omp_one_step_move (sotl_device_t *dev)
{
  
{
  // Apply gravity force
  //
  if (gravity_enabled)
    omp_gravity (dev);

  // Compute interactions between atoms
  //
  if (force_enabled)
    omp_force_cube (dev); // omp_force (dev);

  // Bounce on borders
  //
  if(borders_enabled)
    omp_bounce (dev);

  // Update positions
  //
  omp_move (dev);

#ifdef HAVE_LIBGL
  // Update OpenGL position
  //
  if (dev->display)
    omp_update_vbo (dev);
#endif
}
}

void omp_init (sotl_device_t *dev)
{

#ifdef _SPHERE_MODE_
  sotl_log(ERROR, "Sequential implementation does currently not support SPHERE_MODE\n");
  exit (1);
#endif
  first_touch(dev);
  borders_enabled = 1;

  dev->compute = SOTL_COMPUTE_OMP; // dummy op to avoid warning
}

void omp_alloc_buffers (sotl_device_t *dev)
{
  atom_state = calloc(dev->atom_set.natoms, sizeof(int));
  printf("natoms: %d\n", dev->atom_set.natoms);
}

void omp_finalize (sotl_device_t *dev)
{
  free(atom_state);

  dev->compute = SOTL_COMPUTE_OMP; // dummy op to avoid warning
}

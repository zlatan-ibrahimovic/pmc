#include <stdio.h>
#include "color.h"
#include <string.h>
#include <sys/time.h>
#ifdef __APPLE__
#  include <OpenCL/opencl.h>
#else
#  include <CL/opencl.h>
#endif
#include "util.h"

#define MAX_PLATFORMS 3
#define MAX_DEVICES   5

#define ITERATIONS    100

// Application-specific data
//
#define color(param) printf("\033[%sm",param)
#define KERNEL_NAME  "prescan"

unsigned SIZE = 1024;
unsigned TILE = 16;
unsigned TILE2 = 1;

int *A_data, *B_data;
int n_data;

int print_A= 1024;
int print_B= 1024;
cl_mem A_buffer;  // device memory used for input data
cl_mem B_buffer;  // device memory used for output data
cl_mem n_buffer;  // device memory used for  data

				  
int GROUP_SIZE = 256;
unsigned int ElementsAllocated = 0;
unsigned int LevelsAllocated = 0;

static void alloc_buffers_and_user_data(cl_context context)
{
  // CPU side
  A_data = malloc(SIZE * sizeof(int));
  B_data = malloc((SIZE+1) * sizeof(int));
  //n_data = malloc(SIZE * SIZE * sizeof(float));

  srand(1234);
  printf("*************** \n");

  for(int i = 0; i < print_A; i++) {
    A_data[i] = rand()%10;
    printf(" [%d] ", A_data[i]);
   
  }
  
  printf("\n*************** \n");
  
  n_data = SIZE;
  n_buffer = clCreateBuffer(context,  CL_MEM_READ_WRITE,  sizeof(int), NULL, NULL);
  if (!n_buffer)
    error("Failed to allocate input buffer n");

  // Allocate buffers inside device memory
  //
  A_buffer = clCreateBuffer(context,  CL_MEM_READ_WRITE,  sizeof(int) * SIZE, NULL, NULL);
  if (!A_buffer)
    error("Failed to allocate input buffer A");

  B_buffer = clCreateBuffer(context,  CL_MEM_READ_WRITE,  sizeof(int)  * (SIZE+1), NULL, NULL);
  if (!B_buffer)
    error("Failed to allocate input buffer B");
}

static void check_output_data(void)
{
  unsigned correct = 1;
  int i;
  struct timeval t1,t2;
  double timeInMicroseconds;
  
  for(i = 1; i < SIZE; i++) {
    int expected = A_data[i]+B_data[i-1];
        if(B_data[i] == expected)
	  correct++;
  }
   printf("\tComputed '%d/%d' correct values!\n", correct, i);
  gettimeofday (&t1,NULL);
  color("32");
  //for (unsigned iter = 0; iter < ITERATIONS; iter++) 
    for(int i = 0; i < print_B; i++) {
      //B_data[i]= A_data[i] + A_data[i-1];
       printf("\t[%d]",B_data[i]);
    }
    printf("\n");
    color("0");
  gettimeofday (&t2,NULL);
  
  // Check performance
  //
  timeInMicroseconds = (double)TIME_DIFF(t1, t2) / ITERATIONS;
  
  printf("\tcalcul done in %lf µs over one cpu\n",
	 timeInMicroseconds);
  
}

static void free_buffers_and_user_data(void)
{
  free(A_data);
  free(B_data);
  //free(n_data);

  clReleaseMemObject(A_buffer);
  clReleaseMemObject(B_buffer);
  clReleaseMemObject(n_buffer);
}

static void send_input(cl_command_queue queue)
{
  cl_int err;

  err = clEnqueueWriteBuffer(queue, A_buffer, CL_TRUE, 0,
			     sizeof(int) * SIZE, A_data, 0, NULL, NULL);
  
  err = clEnqueueWriteBuffer(queue, n_buffer, CL_TRUE, 0,
			     sizeof(int) , &n_data, 0, NULL, NULL);
  check(err, "Failed to write to input array A");

}

static void retrieve_output(cl_command_queue queue)
{
  cl_int err;
  //int i;
  err = clEnqueueReadBuffer(queue, B_buffer, CL_TRUE, 0,
			    sizeof(int) * SIZE, B_data, 0, NULL, NULL );  
  check(err, "Failed to read output array B");
  color("5");
  printf("out : ");
  color("0");
  
}

int main(int argc, char** argv)
{
  cl_platform_id pf[MAX_PLATFORMS];
  cl_uint nb_platforms = 0;
  cl_int err;                            // error code returned from api calls
  cl_device_type device_type = CL_DEVICE_TYPE_ALL;


  //int tab[] = {1,2,3,4,5,6,7,8,9,10};

  
  // Filter args
  //
  argv++;
  while (argc > 1) {
    if(!strcmp(*argv, "-g") || !strcmp(*argv, "--gpu-only")) {
      if(device_type != CL_DEVICE_TYPE_ALL)
	error("--gpu-only and --cpu-only can not be specified at the same time\n");
      device_type = CL_DEVICE_TYPE_GPU;
    } else if(!strcmp(*argv, "-c") || !strcmp(*argv, "--cpu-only")) {
      if(device_type != CL_DEVICE_TYPE_ALL)
	error("--gpu-only and --cpu-only can not be specified at the same time\n");
      device_type = CL_DEVICE_TYPE_CPU;
    } else if(!strcmp(*argv, "-s") || !strcmp(*argv, "--size")) {
      unsigned i;
      int r;
      char c;

      r = sscanf(argv[1], "%u%[mMkK]", &SIZE, &c);

      if (r == 2) {
	if (c == 'k' || c == 'K')
	  SIZE = 1024;
	else if (c == 'm' || c == 'M')
	  SIZE = 1024;
      }

      argc--; argv++;
    } else
      break;
    argc--; argv++;
  }

  if(argc > 1)
    TILE = atoi(*argv);

  if(argc > 2)
    TILE2 = atoi(argv[1]);

  // Get list of OpenCL platforms detected
  //
  err = clGetPlatformIDs(3, pf, &nb_platforms);
  check(err, "Failed to get platform IDs");

  printf("%d OpenCL platforms detected\n", nb_platforms);

  // For each platform do
  //
  for (cl_int p = 0; p < nb_platforms; p++) {
    cl_uint num;
    int platform_valid = 1;
    char name[1024], vendor[1024];
    cl_device_id devices[MAX_DEVICES];
    cl_uint nb_devices = 0;
    cl_context context;                 // compute context
    cl_program program;                 // compute program
    cl_kernel kernel;

    err = clGetPlatformInfo(pf[p], CL_PLATFORM_NAME, 1024, name, NULL);
    check(err, "Failed to get Platform Info");

    err = clGetPlatformInfo(pf[p], CL_PLATFORM_VENDOR, 1024, vendor, NULL);
    check(err, "Failed to get Platform Info");

    printf("Platform %d: %s - %s\n", p, name, vendor);

    // Get list of devices
    //
    err = clGetDeviceIDs(pf[p], device_type, MAX_DEVICES, devices, &nb_devices);
    printf("nb devices = %d\n", nb_devices);

    if(nb_devices == 0)
      continue;

    // Create compute context with "device_type" devices
    //
    context = clCreateContext (0, nb_devices, devices, NULL, NULL, &err);
    check(err, "Failed to create compute context");

    // Load program source into memory
    //
    const char	*opencl_prog;
    opencl_prog = file_load(KERNEL_FILE);

    // Attach program source to context
    //
    program = clCreateProgramWithSource(context, 1, &opencl_prog, NULL, &err);
    check(err, "Failed to create program");

    // Compile program
    //
    {
      char flags[1024];

      sprintf (flags,
	       "-cl-mad-enable -cl-fast-relaxed-math -DSIZE=%d -DTILE=%d -DTYPE=%s",
	       SIZE, TILE, "int");

      err = clBuildProgram (program, 0, NULL, flags, NULL, NULL);
      if(err != CL_SUCCESS) {
	size_t len;

	// Display compiler log
	//
	clGetProgramBuildInfo(program, devices[0], CL_PROGRAM_BUILD_LOG, 0, NULL, &len);
	{
	  char buffer[len+1];

	  fprintf(stderr, "--- Compiler log ---\n");
	  clGetProgramBuildInfo(program, devices[0], CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, NULL);
	  fprintf(stderr, "%s\n", buffer);
	  fprintf(stderr, "--------------------\n");
	}
	if(err != CL_SUCCESS)
	  error("Failed to build program!\n");
      }
    }

    // Create the compute kernel in the program we wish to run
    //
    kernel = clCreateKernel(program, KERNEL_NAME, &err);
    check(err, "Failed to create compute kernel");

    // Allocate and initialize input data
    //
    alloc_buffers_and_user_data(context);

    // Iterate over devices
    //
    for(cl_int dev = 0; dev < nb_devices; dev++) {
      cl_command_queue queue;

      char name[1024];
      cl_device_type dtype;

      err = clGetDeviceInfo(devices[dev], CL_DEVICE_NAME, 1024, name, NULL);
      check(err, "Cannot get type of device");
      err = clGetDeviceInfo(devices[dev], CL_DEVICE_TYPE, sizeof(cl_device_type), &dtype, NULL);
      check(err, "Cannot get type of device");

      printf("\tDevice %d : %s [%s]\n", dev, (dtype == CL_DEVICE_TYPE_GPU) ? "GPU" : "CPU", name);

      // Create a command queue
      //
      queue = clCreateCommandQueue(context, devices[dev], CL_QUEUE_PROFILING_ENABLE, &err);
      check(err,"Failed to create command queue");

      // Write our data set into device buffer
      //
      send_input(queue);

      // Execute kernel
      //
      {
	cl_event prof_event;
	cl_ulong start, end;
	struct timeval t1,t2;
	double timeInMicroseconds;
	size_t global = SIZE;  // global domain size for our calculation
	size_t local = SIZE ;   // local domain size for our calculation
	/*	size_t shared;
	int group_index=0;
	int base_index=0;
	*/
	unsigned int count = local;
       
	printf("\t%d Threads in workgroups of %d\n", global, local);

	// Set kernel arguments
	//
	err = 0;
	err  = clSetKernelArg(kernel, 0, sizeof(cl_mem), &A_buffer);
	err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &B_buffer);
       	err |= clSetKernelArg(kernel, 2, sizeof(cl_int), &count);

	check(err, "Failed to set kernel arguments");

	 err = clGetKernelWorkGroupInfo(kernel, devices[dev], CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to retrieve kernel work group info! %d\n", err);
        exit(1);
    }
	gettimeofday (&t1, NULL);

	for (unsigned iter = 0; iter < ITERATIONS; iter++) {
	  err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global, &local,
				       0, NULL, NULL);
	  check(err, "Failed to execute kernel");
	   }

	// Wait for the command commands to get serviced before reading back results
	//
	clFinish(queue);

	gettimeofday (&t2,NULL);

	// Check performance
	//
	timeInMicroseconds = (double)TIME_DIFF(t1, t2) / ITERATIONS;

	printf("\tComputation performed in %lf µs over device #%d\n",
	       timeInMicroseconds,
	       dev);

//	clReleaseEvent(prof_event);
      }

      // Read back the results from the device to verify the output
      //
      retrieve_output(queue);

      // Validate computation
      //
      check_output_data();

      clReleaseCommandQueue(queue);
    }

    // Cleanup
    //
    free_buffers_and_user_data();

    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseContext(context);
  }


  return 0;
}

    __kernel  void prescan(__global const int *A,__global int *B,const  uint n)  
    {  
        __local int temp[SIZE];  // allocated on invocation  
        int thid = get_local_id(0);  
    	int offset = 1;  
	int last = 

    	temp[2*thid] = A[2*thid]; // load input into shared memory  
    	temp[2*thid+1] = A[2*thid+1];  
  	
    	for (int d = n>>1; d > 0; d >>= 1)                    // build sum in place up the tree  
    	{   
    	    barrier(CLK_LOCAL_MEM_FENCE);
       	    if (thid < d)  
       	    {  
	        int ai = offset*(2*thid+1)-1;  
		int bi = offset*(2*thid+2)-1;  
		temp[bi] += temp[ai];  		       
    	    }  
    	    offset *= 2;  
   
	    if (thid == 0) { temp[n - 1] = 0; } // clear the last element  
         }            
	 for (int d = 1; d < n; d *= 2) // traverse down tree & build scan  
    	 {  
     	       offset >>= 1;  
               barrier(CLK_LOCAL_MEM_FENCE);
               if (thid < d)                       
               {  
	       	  int ai = offset*(2*thid+1)-1;  
    	  	  int bi = offset*(2*thid+2)-1;  
		  float t = temp[ai];  
    	  	  temp[ai] = temp[bi];  
    	  	  temp[bi] += t;   
          	}  
    	 }  
     	 barrier(CLK_GLOBAL_MEM_FENCE);

	 B[2*thid-1] = temp[2*thid]; // write results to device memory  
	 B[2*thid] = temp[2*thid+1];  
	 barrier(CLK_GLOBAL_MEM_FENCE);	 
	 B[SIZE - 1] = temp[SIZE - 1] + A[SIZE - 1];



    }  
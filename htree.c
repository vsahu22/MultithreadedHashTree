#include <stdio.h>     
#include <stdlib.h>   
#include <stdint.h>
#include <string.h>
#include <inttypes.h>  
#include <errno.h>     // for EINTR
#include <fcntl.h>     
#include <unistd.h>    
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h> // For mmap
#include <sys/syscall.h>
#include <pthread.h> // for threads
// To help w threads
#include "common.h" 
#include "common_threads.h"

// Print out the usage of the program and exit.
void Usage(char*);
uint32_t jenkins_one_at_a_time_hash(const uint8_t* , uint64_t );
void* computeHashValue(void* arg);

// block size
#define BSIZE 4096

// These variables need to by accessed by the threads, but don't need to be changed within them
uint8_t* fileData = NULL; // Used with mmap that contains the mapping to the virtual address of the file
uint64_t readLength = 0; // Number of bytes that will be read per thread (size of the total amount of blocks)
int numOfThreads  = 0; // Variable that will store the total number of threads

int 
main(int argc, char** argv) 
{
  int32_t fd;
  uint32_t nblocks;
  struct stat sb; // Used for finding the file size
  size_t fileSize = 0; // Used to store file size
  int threadNum = 0; // Passed to initial thread as an argument, for thread 0
  uint64_t blockReadNum = 0; // Number of blocks that a thread will read

  // input checking 
  if (argc != 3)
    Usage(argv[0]);

  // open input file
  fd = open(argv[1], O_RDWR);
  if (fd == -1) {
    perror("open failed");
    exit(EXIT_FAILURE);
  }
  // use fstat to get file size
  if (fstat(fd, &sb) == -1) {
  	perror("fstat error");
  }
  fileSize = sb.st_size;

  // calculate nblocks 

  if (fileSize % BSIZE != 0) { // Number of blocks is exactly fileSize / BSIZE is fileSize is divisible by BSIZE, otherwise add 1 block
  	nblocks = (fileSize / BSIZE) + 1;
  }
  else {
  	nblocks = (fileSize / BSIZE);
  }
  numOfThreads = atoi(argv[2]); // Number of threads user inputs to calculate hash value
  if (numOfThreads < 1) {
  	printf("Error: Invalid number of threads, must be greater than 0\n");
	exit(EXIT_FAILURE);
  }
  blockReadNum = nblocks / numOfThreads; // Number of blocks each thread will read
  readLength = blockReadNum * BSIZE; // Read length is the amount of bytes each thread will read

  printf(" no. of blocks = %u \n", nblocks);

  double start = GetTime();

  // calculate hash value of the input file
  
  fileData = mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0); // Using mmap to create a pointer to read from the file
  if (fileData == MAP_FAILED) {
  	perror("mmap error");
  }
  char* hashValueString;

  pthread_t root;
  Pthread_create(&root, NULL, computeHashValue, &threadNum); // Creating root thread that calls function to compute hash value
  Pthread_join(root, (void**) &hashValueString); // When it finishes executing, the computed hash value is returned to hashValueString

  double end = GetTime();
  
  uint32_t hash = strtoul(hashValueString, NULL, 0); // Converting the string hash value that was returned to an unsigned int
  free(hashValueString); // The value returned from the threads is dynamically allocated, so it must be released

  printf("hash value = %u \n", hash);
  printf("time taken = %f \n", (end - start));
  close(fd);
  return EXIT_SUCCESS;
}

// Thread function that computes hash value
void* computeHashValue(void* arg) {
	int threadsCreated = 0;
	int threadNum = *(int*) arg; // Casting void argument which is arg to int
	int leftThread = threadNum * 2 + 1; // Left child thread number
	int rightThread = threadNum * 2 + 2; // Right child thread number
	pthread_t left;
	pthread_t right;
	if (leftThread < numOfThreads) { // If the left child number is less than the total number of threads, create left child
		threadsCreated++;
		Pthread_create(&left, NULL, computeHashValue, &leftThread); // Pthread already does error checking, so no need (common_threads.h)
	}
	if (rightThread < numOfThreads) { // If the right child number is less than the total number of threads, create right child
		threadsCreated++;
		Pthread_create(&right, NULL, computeHashValue, &rightThread);
	}

	uint8_t* threadFileRead = fileData; // This will point to where mmap points to for file, so each thread will have a unique pointer variable
	uint64_t readStart = threadNum * readLength; // This is the offset of where the thread will start to read the string
	threadFileRead += readStart; // Setting the pointer of the read location to the offset of where the file is read
	uint32_t intHashValue = jenkins_one_at_a_time_hash(threadFileRead, readLength); // Reading and calculating hash value
	char* hashValue = (char*) malloc(sizeof(char) * 50); // Dynamically allocating sufficient space to store hashValue as a string
	if (hashValue == NULL) {
		perror("Malloc failed");
	}
	sprintf(hashValue, "%u", intHashValue); // Converting the uint32_t hash value to a string
	char* leftHashValue = NULL;
	char* rightHashValue = NULL;
	if (threadsCreated > 0) { // If threads were created in this thread, then this thread needs to wait for them to terminate
		Pthread_join(left, (void**) &leftHashValue); // Waiting for left thread to terminate and getting hash value from it
		strcat(hashValue, leftHashValue); // Concatenating the left thread value with the current thread hash value
		free(leftHashValue); // Since the return value was dynamically allocated in previous thread, it needs to be freed
		if (threadsCreated == 2) { // If more than one thread was created, that means it needs to wait for the other thread to terminate
			Pthread_join(right, (void**) &rightHashValue); // Waiting for right thread to terminate and getting hash value from it
			strcat(hashValue, rightHashValue); // Concatenating the right thread hash value with the current + left thread hash value
			free(rightHashValue); // Freeing the dynamically allocated return value from previous thread
		}
	}
	if (leftHashValue != NULL || rightHashValue != NULL) { // Computing new hash value if a hash value is passed back from one of the children threads
		uint8_t* newIntHashValue = (uint8_t*) hashValue;
		intHashValue = jenkins_one_at_a_time_hash(newIntHashValue, strlen(hashValue));
	}
	char* newHashValue = (char*) malloc(sizeof(char) * 50); // Dynamically allocating space for the hash value this thread will send back
	if (newHashValue == NULL) {
		perror("Malloc failed");
	}
	sprintf(newHashValue, "%u", intHashValue); // If there is a new hash value, it is converted to a string
	free(hashValue); // Freeing the space that was dynamically allocated for the hash value
	return (void*) newHashValue; // Sending hash value back to parent thread

}

uint32_t 
jenkins_one_at_a_time_hash(const uint8_t* key, uint64_t length) 
{
  uint64_t i = 0;
  uint32_t hash = 0;

  while (i != length) {
    hash += key[i++];
    hash += hash << 10;
    hash ^= hash >> 6;
  }
  hash += hash << 3;
  hash ^= hash >> 11;
  hash += hash << 15;
  return hash;
}


void 
Usage(char* s) 
{
  fprintf(stderr, "Usage: %s filename num_threads \n", s);
  exit(EXIT_FAILURE);
}

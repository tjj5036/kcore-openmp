#define _GNU_SOURCE
//define OUTPUT
//#define DEBUG
#include <omp.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#ifdef DEBUG
#  define D(x) x
#else
#  define D(x) 
#endif

#ifdef OUTPUT
#  define O(x) x
#else
#  define O(x) 
#endif
	
typedef struct edge {
	/* Edge container.
	 * Also doubles as a message.
	 */
	int id;
	int est;
	struct edge *next;
} edge;

typedef struct graph_node {
	/* A vertex in a graph.
	 */
	int id;
	int valid;
	int kcore;
	int active;
	struct edge *edge_head;
	struct edge *unprocessed_msgs;
	omp_lock_t lock;

} graph_node;

int computeIndex(graph_node *A, int id, int k) {
	/* Generates an estimate of the current k-core value.
	 */
	int i;
	int count[k+1];
	for (i = 0; i < k+1; i++) {
		count[i] = 0;
	}

	edge *neb;
	for (neb = A[id].edge_head; neb != NULL; neb=neb->next) { //for each v in neighbor
		int j = k;
		if (neb->est < k) {
			j = neb->est; //j = min(k, est[v])
		}
		count[j] = count[j] + 1;
	}

	for (i = k; i > 1; i--) {
		count[i-1] = count[i-1] + count[i];
	}

	i = k;
	while ((i > 1) && (count[i] < i)) {
		i = i - 1;
	}
	return i;
}

int process_message(int id, graph_node *A) {
	/* Iterates through all received messages and deletes them as they go.
	 */

	edge *rec_msg = A[id].unprocessed_msgs;
	edge *next_msg;
	edge *local_est;
	int ret = 0;
	A[id].active = 0;
	 
	while (rec_msg != NULL) {
		next_msg = rec_msg->next;
		for (local_est = A[id].edge_head; local_est != NULL; local_est = local_est->next) {

			if (local_est->id == rec_msg->id) { 

				if (rec_msg->est < local_est->est) {

					local_est->est = rec_msg->est; 
					int t = computeIndex(A, id, A[id].kcore);

					if (t < A[id].kcore) {
						ret = 1;
						A[id].kcore = t;
						A[id].active = 1;
						D(printf("%d is setting core to %d\n", id, t));
					}
				}
			}

		}
		free(rec_msg);
		rec_msg = next_msg;
	}
	A[id].unprocessed_msgs = NULL;
	return ret;
}

void send_message(int to_id, int from_id, int value, graph_node *A) {
	/* Sends a message to an id. 
	 */

	omp_set_lock(&A[to_id].lock);
	struct edge *new_message = NULL;
	new_message = malloc(sizeof(edge));
	new_message->id = from_id;
	new_message->est = value;
	new_message->next = A[to_id].unprocessed_msgs;
	A[to_id].unprocessed_msgs = new_message;
	omp_unset_lock(&A[to_id].lock);
}

int init_cores(graph_node *A, char *graph_file) {
	/* Reads in the graph file and initializes core values.
	 */
	FILE *fp;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	fp = fopen(graph_file, "r");
	if (NULL == fp) {
		D(fprintf(stderr, "Cannot open graph file!\n"));
		return 0;
	}

	char * ptr;
	int counter;
	while ((read = getline(&line, &len, fp)) != -1) {
		/* Read line by line - don't know actual line length.
		 * Each line corresponds to a vertex id and the edge ids.
		 * Ex: "1 2 3 4" would be a vertex with id 1 and edge ids 2 3 and 4
		 */
		ptr = strtok(line," ");
		counter = 0;
		graph_node new_node;

		while (ptr != NULL) {

			if (0 == counter) {

				new_node.id = atoi(ptr);
				new_node.edge_head = NULL;
				new_node.unprocessed_msgs = NULL;
				new_node.active = 1;
				omp_init_lock(&new_node.lock);

			} else {

				edge *new_edge;
				new_edge = (edge*)malloc(sizeof(edge));
				new_edge->id = atoi(ptr);
				if (0 == new_edge->id) {
					D(printf("Invalid vertex id!\n"));
					exit(0);
				}
				new_edge->est = INT_MAX;
				new_edge->next = new_node.edge_head;
				new_node.edge_head = new_edge;

			}

			ptr = strtok(NULL," ");
			counter++;
		}
		new_node.kcore = counter-1;
		new_node.valid = 1;
		A[new_node.id] = new_node;

		D(printf("New node with id: %d, kcore %d, \n", new_node.id, new_node.kcore));
		counter = 0;
	}

	if (line) {
		free(line);
	}

	return 1;
}

void compute_k_core(int n, graph_node *A) {
	/* Computes the K-Core. The idea is to divide the input array
	 * into somewhat equal chunks. Each thread processes the vertices
	 * in its assigned chunk, and generates estimates. There is a "compute"
	 * phase and a "send" phase. Without differentiating some vertices become
	 * stuck.
	 */
	int supersteps = 0;
	int working_threads = 4;
	if (working_threads > n) {
		// The chances of this happening are very slim.
		working_threads = 1;
	}
	D(printf("Initializing with %d\n", working_threads));
	int slice_size = n / working_threads;

	int offsets[working_threads];
	int i;
	for (i = 0; i < working_threads; i++) {
		offsets[i] = 0;
	}
	int continue_itr = 1;

	while (continue_itr) {
		#pragma omp parallel num_threads(working_threads)
		{
			int tid = omp_get_thread_num();
			int slice_start = 1 +(tid * slice_size);
			int slice_end = slice_start+slice_size;
			if (tid == working_threads-1) {
				slice_end = n;
			}

			D(fprintf(stderr, "thread %d: start: %d, end:%d\n",
				tid, slice_start, slice_end
			));

			int z;
			for (z = slice_start; z < slice_end; z++) {
				/* Want to stride across assigned blocks.
				 */
				if (A[z].valid) {

					if (0 == supersteps) {
						/* Send first round of messages- local coreness initialized
						 * when the file was first read
						 */
						edge *curr;
						for (curr = A[z].edge_head; curr != NULL; curr=curr->next) {
							send_message(curr->id, z, A[z].kcore, A);
						}
					
					} else {
						if (1 == (supersteps & 1)) {
							/* Process message phase.
							 */
							offsets[tid] += process_message(z, A);

						} else {
							/* Send message phase.
							 */
							if (A[z].active) {
								edge *curr;
								for (curr = A[z].edge_head; curr != NULL; curr=curr->next) {
									send_message(curr->id, z, A[z].kcore, A);
									offsets[tid] = 1;
								}
							} 
							
							else {
								D(printf("%d is not active\n", z));
							}
						}
					}
				}

			}
		} 

		#pragma omp barrier

		#pragma omp single
		{
			if (supersteps != 0) {
				continue_itr = 0;
				for (i = 0; i < working_threads; i++) {
					if (offsets[i] != 0) {
						D(printf("Need to continue\n"));
						continue_itr = 1;
					}
					offsets[i] = 0;
				}
			}
			supersteps++;
		}
	}
	
	for (i = 1; i < n; i++) {
		if (A[i].valid) {
			O(printf("ID: %d, KCore: %d\n", i, A[i].kcore));
		}

		D(printf("ID: %d, msg estimates: " , i));
		edge *msg;
		for (msg= A[i].unprocessed_msgs; msg != NULL; msg=msg->next) {
			D(printf("%d : %d; ", msg->id, msg->est));
		}
		D(printf("\n"));
	}
	printf("%d number of suoersteps", supersteps);

}

int main (int argc, char *argv[]) {
	/* Driver: takes in a graph file and the number of vertices in that file.
	 * That is, it takes the HIGHEST vertex id. So if you have a graph
	 * with vertices 1, 2, and 7, you would enter 7 as the second argument.
	 * Not 3! Note that this program does hardly any error checking.
	 * It is also assumed that 0 is not a valid vertex id.
	 */
	if (argc < 3) {
		printf("usage: %s <graph-file> <number of vertices>\n", argv[0]);
		return 1;
	}
	int i;
	int N = atoi(argv[2])+1;
	graph_node *A = NULL;
	A = malloc(sizeof(graph_node) * N);
	
	#pragma omp parallel
		{
		#pragma omp for
		for (i = 0; i < N; i++) {
			/* This is done to avoid "missing" vertices.
			 * If you have few vertices that all have high identifers, this eats
			 * up considerable unnecessary space.
			 */ 
			graph_node node;
			node.valid = 0;
			node.kcore = 0;
			node.edge_head = NULL;
			node.unprocessed_msgs = NULL;
			node.active = 0;
			omp_init_lock(&node.lock);
			A[i] = node;
		}
	}

	init_cores(A, argv[1]);
	double start1 = omp_get_wtime();
	compute_k_core(N, A);
	double end1 = omp_get_wtime();
	printf("Parallel: Elapsed: %f seconds\n", end1-start1);
	return 0;
}

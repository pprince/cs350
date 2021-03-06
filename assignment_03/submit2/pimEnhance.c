/* Includes */
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


#include "im_shared.h"		/* all the stuff shared between both versions (threads and processes) */

typedef struct {
	int row, col;
	double mean_value, median_value, enhanced_value, stddev_value;
} image_result_t;

/* Prototypes */
void *iterate_input_image(void);
int get_child_id(void);


/* Global Variables */
pid_t *children;	/* array of children			*/
int fd[2];		/* pipe for child->parent communication	*/


/* main() */
int main(int argc, char **argv) {

	int i, j; /* loop counters */


	/* Basic stuff; see elsewhere */
	check_args(argc, argv);
	read_input_image(argv[1]);
	calc_overall_statistics();
	allocate_image_memory();


	/* Allocate our array of child PID objects. */
	unless( children = (pid_t *)malloc(num_threads * sizeof(pid_t)) ){
		perror("Couldn't allocate memory");
		exit(43);
	}


	/* Create our pipe. */
	unless( pipe(fd) == 0 ) {
		perror("Error creating pipe");
		exit(44);
	}


	/* Spawn child processes. */
	for (i=0 ; i<num_threads ; i++){
		switch( children[i] = fork() ){
			case -1:
				perror("fork() call failed");
				exit(42);
				break;
			case 0:
				/* child process */
				children[i] = getpid();
				close(fd[0]); /* close unused read end. */
				iterate_input_image(); /* will not return. */
		}
				
	}

	close(fd[1]); /* close unused write end. */

	/* Read image data from children. */
	image_result_t res;
	while( read(fd[0], &res, sizeof(image_result_t)) > 0 ){
		Mean_Image[res.row*cols+res.col]     = res.mean_value;
		Median_Image[res.row*cols+res.col]   = res.median_value;
		Enhanced_Image[res.row*cols+res.col] = res.enhanced_value;
		Variance_Image[res.row*cols+res.col] = res.stddev_value;
	}

	/* Wait for all child threads to complete. */
	for (i=0 ; i<num_threads ; i++){
		int child_exit_status;
		waitpid(children[i], &child_exit_status, 0);
		if(child_exit_status != 0){
			fprintf(stderr, "Child process returned non-zero status.");
			exit(41);
		}
	}


	/* Assignment requires some more output. */
	calc_and_print_additional_statistics();


	/* Variance image is too dark, so we multiply it by 2 for human eyes. */
	for (i=0; i<rows; i++) for (j=0; j<cols; j++) Variance_Image[i*cols+j] *= 2;


	/* More basics. */
	write_output_images(argv[2], argv[3], argv[4], argv[5]);
	exit(0); /* with much success! */
}


/* Iterate over the input image, populating the others along the way. */
void *iterate_input_image(void){
	int i, j, c; /* loop counters */
	double window_mean, window_variance, window_median, window_stddev;

	int child_id = get_child_id();

	c = 0;
	image_result_t res;
	for (i=0; i<rows; i++){
		for (j=0; j<cols; j++) {
			c++; /* fixes bug #002 */
			if (c % num_threads == child_id) {
				/* Choose the subregion of interest (window) around this pixel. */
				window_t window = select_window(window_size, i, j, rows, cols);

				/* Calculate the median of just the window. */
				windowcalc_median(Image, window, &window_median);

				/* Calculate the mean and variance of just the window. */
				windowcalc_mean_and_variance(Image, window, &window_mean, &window_variance);

				/* Calculate standard deviation of just the window. */
				window_stddev = sqrt(window_variance);

				/* Calculate the enhanced value of this pixel. */
				int window_enhanced = calc_enhanced( Image[i*cols+j], mean, stddev, window_mean, window_stddev );

				/* Assignments to the output images. */
				res.row = i;
				res.col = j;
				res.mean_value		= window_mean;
				res.median_value	= window_median;
				res.enhanced_value      = window_enhanced;
				res.stddev_value        = window_stddev;
				unless( write(fd[1], &res, sizeof(image_result_t)) == sizeof(image_result_t) ){
					perror("Could not write pixel result to pipe");
					exit(99);
				}
			}
		}
	}

	exit(0);
}


int get_child_id(void) {
        int i;
        int child_id = -1;
        for (i=0; i<num_threads; i++) {
                if( children[i] == getpid() ){
                        child_id = i;
                        break;
                }
        }                                                                                                          
        return child_id;                                                                                          
}


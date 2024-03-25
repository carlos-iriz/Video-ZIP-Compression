# Video-ZIP-Compression
A provided compression tool was made faster using Threads

Video ZIP is a project aimed at speeding up a given video compression tool. The tool takes a folder containing uncompressed image files (with ".ppm" extension) as input and generates a single zip file with all compressed images. This project is a group effort with specific requirements regarding group size and submission guidelines where we used github to collaborate on the project. We were required to speed up the given program using the pthread library and could only run 20 threads at once including the main thread.

Using threads and several locks to get through critical sections we were able to speed up the original program by approximately 5 times its original runtime.

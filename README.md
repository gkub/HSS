# HSS
Histogram Sort with Sampling - experimenting, based on the 2019 University of Illinois Urbana-Champaign paper.

## Usage Instructions
*Tested using ZSH CLI*  
To compile:
```
make clean  && make compile
```
To run:
```
./hss <seed> <workers> <imbalance> <size> [--verbose]
```
Example run:
```
./hss 12345 4 0.1 320000000 --verbose
```
The above example will use random seed 12345, 4 threads/processors, have an imbalance ε of 0.1, and an array of 320 million integers.

# Histogram-based Sample Sort (HSS)

This project implements the Histogram-based Sample Sort (HSS) algorithm, a parallel sorting method based on a 2019 paper from the University of Illinois Urbana-Champaign. HSS sorts large datasets using multiple threads by leveraging sampling and histograms to partition data into balanced buckets, followed by local sorting. The current implementation includes timing for performance analysis but does not yet enforce the load imbalance parameter (ε).

## Usage Instructions

*Tested on a POSIX-compliant system using ZSH CLI*

### Compilation

To compile the program, execute:

```bash
make clean && make compile
```

- **`make clean`**: Removes the existing executable and object files for a clean build.
- **`make compile`**: Builds `hss.cpp` into an executable named `hss` using `g++` with C++17, pthread support, and `-O3` optimization.

It’s recommended to run `make clean` before `make compile` after modifying the source code to ensure a fresh build.

### Running the Program

Run the compiled executable with:

```bash
./hss <seed> <workers> <imbalance> <size> [--verbose]
```

Arguments:
- **`<seed>`**: Integer seed for the random number generator (e.g., 12345). Controls dataset shuffling for reproducibility.
- **`<workers>`**: Number of parallel threads (e.g., 4). Determines how many workers process the data.
- **`<imbalance>`**: Maximum allowed load imbalance ratio (ε), a float (e.g., 0.1). Currently parsed but not enforced.
- **`<size>`**: Number of integers to sort (e.g., 320000000). Sets the dataset size.
- **`[--verbose]`**: Optional flag to enable detailed debug output, including intermediate steps and timing.

#### Examples

- Basic execution:
  ```bash
  ./hss 12345 4 0.1 320000000
  ```
  - Seed: 12345
  - Workers: 4
  - Imbalance: 0.1
  - Size: 320 million integers
  - Output: Validation result only.

- With debug output:
  ```bash
  ./hss 42 4 0.1 1000000 --verbose
  ```
  - Seed: 42
  - Workers: 4
  - Imbalance: 0.1
  - Size: 1 million integers
  - Output: Detailed logs and timing.

**Note**: Arguments must be provided in this order. Missing or misordered arguments will trigger an error message.

## Program Description

HSS is a parallel sorting algorithm that distributes a large dataset across multiple threads for efficient sorting. It uses sampling to estimate data distribution, partitions the data into buckets based on selected splitters, and sorts each bucket locally. The program generates a skewed dataset (squared integers) without duplicates, shuffles it, and validates the sorted result.

### Algorithm Phases

The algorithm executes in four phases, each timed for performance analysis:

1. **Initial Partitioning and Local Sorting**
   - The dataset is divided into `<workers>` equal chunks.
   - Each worker sorts its chunk using `std::sort`.
   - Result: Sorted sub-arrays per worker.

2. **Splitter Selection**
   - Each worker samples its sorted chunk (10 samples per worker times `<workers>`).
   - Samples are collected into a shared pool using a mutex.
   - Worker 0 sorts the samples and selects `<workers> - 1` splitters at regular intervals.
   - Result: Splitters defining bucket boundaries.

3. **Partition and Exchange**
   - Workers partition their chunks into `<workers>` buckets based on splitters using `std::upper_bound`.
   - Data is exchanged via a shared structure with per-bucket mutexes.
   - Result: Each worker receives its assigned bucket.

4. **Final Sorting**
   - Each worker sorts its bucket using `std::sort`.
   - Result: Sorted buckets that collectively form the sorted dataset.

### Imbalance Parameter (ε)
- **Definition**: ε represents the maximum allowed load imbalance ratio, where the largest bucket should not exceed `(total_elements / workers) * (1 + ε)`.
- **Current State**: Parsed as `<imbalance>` but not enforced. The algorithm performs one splitter selection round without refinement.
- **Future Use**: Could trigger splitter adjustments if buckets exceed the ε threshold.

### Validation
- The program concatenates the sorted buckets, sorts the result, and compares it to a sorted copy of the original dataset to confirm correctness.
- Timing for each phase and total execution is reported.

## Additional Notes

- **Load Imbalance**: Without ε enforcement, bucket sizes may vary significantly, especially with skewed data. Adding refinement rounds could address this.
- **Timing Data**: Phase durations are logged (max across workers) to analyze performance. Use `--verbose` for detailed output.
- **Dataset**: Generated as unique squared integers (1² to N²) and shuffled, ensuring no duplicates with a skewed distribution.
- **Scalability**: Designed for large datasets; adjust `<size>` and `<workers>` to test performance limits.
- **Contribution**: Feedback, optimizations, or enhancements are welcome via pull requests.

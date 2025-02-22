# Histogram-based Sample Sort (HSS)

Welcome to **HSS**—an experimental playground for the Histogram Sort with Sampling algorithm, inspired by a 2019 paper from the University of Illinois Urbana-Champaign. This program tackles parallel sorting with a clever mix of sampling and histograms, aiming to balance workloads across multiple processors like a maestro conducting a symphony of threads. Whether you're here to experiment, optimize, or just geek out over sorting algorithms, this is the place to be!

## Usage Instructions

*Tested using ZSH CLI on a POSIX-compliant system*

### Compilation

To build the program from scratch, run:

```bash
make clean && make compile
```

- **`make clean`**: Wipes out any existing executable or object files to ensure a fresh start.
- **`make compile`**: Compiles `hss.cpp` into an executable named `hss` using `g++` with C++17, pthread support, and optimization level `-O3`.

**Pro Tip**: Always run `make clean` before `make compile` if you’ve tweaked the source code, just to avoid any lingering build artifacts.

### Running the Program

Once compiled, launch the program with:

```bash
./hss <seed> <workers> <imbalance> <size> [--verbose]
```

Here’s what each argument means:

- **`<seed>`**: An integer (e.g., 12345) that seeds the random number generator. Use the same seed to get reproducible results—perfect for debugging or benchmarking!
- **`<workers>`**: The number of parallel workers (threads) to unleash on the sorting task. Think of this as the size of your sorting squad (e.g., 4).
- **`<imbalance>`**: The maximum allowed load imbalance ratio, denoted ε (epsilon). It’s a float (e.g., 0.1) that sets how much slack you’ll tolerate in workload distribution. *Note*: This is parsed but not yet enforced in the current implementation—more on that later!
- **`<size>`**: The total number of integers to sort (e.g., 320000000). Go big or go home—this is the scale of your sorting adventure.
- **`[--verbose]`**: An optional flag to turn on detailed debug output. Want to peek under the hood? Add this!

#### Example Runs

- **Basic Run**:
  ```bash
  ./hss 12345 4 0.1 320000000
  ```
  - Seed: 12345
  - Workers: 4 threads
  - Imbalance (ε): 0.1
  - Size: 320 million integers
  - Output: Silent but deadly—sorted data with no chatter.

- **Verbose Run**:
  ```bash
  ./hss 42 4 0.1 1000000 --verbose
  ```
  - Seed: 42 (the answer to life, the universe, and everything)
  - Workers: 4 threads
  - Imbalance (ε): 0.1
  - Size: 1 million integers
  - Output: A flood of debug info to satisfy your curiosity.

**Note**: Arguments must be provided in this exact order—no flags like `-s` or `--seed` here. If you skip `--verbose`, it runs quietly. Mess up the order or miss an argument, and it’ll complain—so double-check your command!

## How the Program Works

The Histogram-based Sample Sort (HSS) algorithm is a parallel sorting beast that blends sampling, histograms, and clever data shuffling to sort massive datasets across multiple threads. It’s designed to keep every worker busy with a fair share of the load, avoiding the chaos of one thread drowning in work while others sip coffee. Here’s the full scoop, broken down into digestible phases.

### Overview of the Algorithm

HSS operates in four slick steps:

1. **Initial Partitioning and Local Sorting**: Split the data and sort each piece locally.
2. **Splitter Selection**: Sample the data to pick pivot points (splitters) that divvy up the workload.
3. **Partition and Exchange**: Shuffle the data into buckets based on those splitters.
4. **Final Sorting**: Polish each bucket into a fully sorted masterpiece.

Together, these phases transform a chaotic pile of integers into a beautifully sorted array, with each thread pulling its weight.

### Detailed Breakdown of Each Phase

#### Phase 1: Initial Partitioning and Local Sorting
- **What Happens**: The dataset (an array of `<size>` integers) is chopped into `<workers>` equal-sized chunks. Imagine slicing a pizza for your thread crew—everyone gets a piece.
- **Execution**: Each worker grabs its chunk and sorts it locally using `std::sort`. No teamwork yet—just every thread flexing its sorting muscles solo.
- **Outcome**: Each worker ends up with a sorted mini-array, ready for the next step.

#### Phase 2: Splitter Selection
- **Sampling**: Each worker picks a small, representative sample from its sorted chunk—like tasting the soup to guess the recipe. These samples are tossed into a shared pool, safely handled with thread synchronization (mutexes, anyone?).
- **Leader’s Role**: Worker 0 (the “leader”) takes charge, sorts the pooled samples, and selects `<workers> - 1` splitters. These splitters are pivot values that aim to split the data into `<workers>` roughly equal buckets.
- **How It’s Done**: The splitters are chosen at even intervals from the sorted samples (e.g., every 1/`<workers>`th percentile), giving a snapshot of the data’s distribution.

#### Phase 3: Partition and Exchange
- **Partitioning**: Each worker revisits its sorted chunk and uses the splitters to assign elements to buckets. It’s like sorting mail into `<workers>` mailboxes, using `std::upper_bound` to find where each element belongs.
- **Exchange**: Workers swap data via a shared structure—think of it as a thread-safe swap meet. Each worker contributes its bucket contents, then grabs the bucket assigned to it after a synchronization barrier.
- **Outcome**: Every worker now owns a bucket of unsorted elements that belong together in the final sorted order.

#### Phase 4: Final Sorting
- **What Happens**: Each worker sorts its bucket locally with `std::sort`, polishing its piece of the puzzle.
- **Result**: When all workers finish, their buckets align perfectly—like a jigsaw puzzle snapping into place—to form the fully sorted dataset.

### The Role of the Imbalance Parameter (ε)
- **Purpose**: ε (epsilon) is your tolerance knob for load imbalance. It’s the max deviation from perfect balance you’re willing to accept (e.g., 0.1 means a bucket can be up to 10% larger than ideal).
- **Theory**: In a textbook HSS, if a bucket’s size exceeds the ε threshold, the algorithm would refine the splitters and try again. It’s a safety net for fairness.
- **Reality Check**: In this implementation, ε is parsed but doesn’t flex its muscles yet. The program does one round of splitter selection and rolls with it, imbalance or not. Future upgrades could make ε the hero it’s meant to be!

### Why It’s Cool
HSS shines by using sampling to guess the data’s shape without sorting everything upfront, then histograms (via buckets) to spread the work evenly. It’s got built-in timing for each phase too, so you can geek out over performance stats—whether you’re chasing speed or hunting bottlenecks.

## Additional Notes

- **Current Quirks**: ε isn’t enforcing balance yet. The buckets might be lopsided, but it still sorts correctly. Refinement rounds could fix this—fancy a challenge?
- **Performance Perks**: Timing data for each phase is logged internally. Use `--verbose` to peek, or hack the code to export it!
- **Join the Fun**: Fork this repo, tweak it, break it, fix it—then send a pull request. Let’s make HSS legendary together!
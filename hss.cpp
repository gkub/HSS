#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <pthread.h>
#include <cmath>
#include <string>
#include <set>
#include <numeric>

// Global configuration structure for shared settings and synchronization
struct Config {
    int num_workers;                    // Number of parallel workers (threads)
    int random_seed;                    // Seed for reproducible randomization
    size_t total_elements;              // Total number of elements to sort
    bool verbose_output;                // Enable detailed debug prints
    std::vector<long long> dataset;     // Original unsorted dataset (using long long for large values)
    std::vector<long long> splitters;   // Selected partition boundaries
    pthread_barrier_t barrier;          // Synchronization barrier for threads
    pthread_mutex_t lock;               // Mutex for shared data protection
    double max_imbalance;               // Allowed load imbalance ratio (ε), not used yet
    
    // For data exchange between workers
    std::vector<std::vector<long long>> bucket_contributions; // [bucket_id][elements]
    std::vector<pthread_mutex_t> bucket_locks;               // One mutex per bucket
};
Config global_config;

// Per-thread execution state
struct WorkerContext {
    int worker_id;                      // Unique worker ID (0 to num_workers-1)
    std::vector<long long> local_chunk; // Subset of data assigned to this worker
    std::vector<long long> local_samples; // Locally sampled pivot candidates
};

// Print debug messages if verbose mode is enabled or forced
void debug_print(const std::string& message, bool force = false) {
    if (global_config.verbose_output || force) {
        std::cerr << "[DEBUG] " << message << "\n";
    }
}

// Print vector contents (limited to first 10 elements for brevity)
void print_vector(const std::string& label, const std::vector<long long>& vec, bool force_verbose = false) {
    if (!global_config.verbose_output && !force_verbose) return;
    std::cerr << "[DEBUG] " << label << " (" << vec.size() << " elements): [";
    for (size_t i = 0; i < std::min(vec.size(), 10UL); ++i) {
        std::cerr << vec[i] << (i < vec.size()-1 ? ", " : "");
    }
    if (vec.size() > 10) std::cerr << "...";
    std::cerr << "]\n";
}

// Worker thread function implementing the HSS algorithm
void* worker_function(void* arg) {
    WorkerContext* ctx = static_cast<WorkerContext*>(arg);
    const int worker_id = ctx->worker_id;
    const size_t dataset_size = global_config.total_elements;
    const int total_workers = global_config.num_workers;

    // Phase 1: Initial Data Partitioning
    // Divide dataset into contiguous chunks for each worker
    const size_t base_chunk_size = dataset_size / total_workers;
    const size_t chunk_start = worker_id * base_chunk_size;
    const size_t chunk_end = (worker_id == total_workers - 1) 
                           ? dataset_size 
                           : chunk_start + base_chunk_size;
    
    // Assign and sort local chunk
    ctx->local_chunk.assign(global_config.dataset.begin() + chunk_start,
                           global_config.dataset.begin() + chunk_end);
    std::sort(ctx->local_chunk.begin(), ctx->local_chunk.end());

    debug_print("Worker " + std::to_string(worker_id) + 
                " initial chunk size: " + std::to_string(ctx->local_chunk.size()));
    print_vector("Worker " + std::to_string(worker_id) + " initial chunk", ctx->local_chunk);

    pthread_barrier_wait(&global_config.barrier);

    // Phase 2: Splitter Selection
    // Sample elements from local chunk for global splitter selection
    const int samples_per_worker = 10 * total_workers; // Increased sample size for better distribution capture
    ctx->local_samples.clear();
    if (ctx->local_chunk.size() >= (size_t)samples_per_worker) {
        std::sample(ctx->local_chunk.begin(), ctx->local_chunk.end(),
                    std::back_inserter(ctx->local_samples), samples_per_worker,
                    std::mt19937{std::random_device{}()});
    } else {
        ctx->local_samples = ctx->local_chunk;
    }

    // Contribute samples to global splitters (thread-safe)
    pthread_mutex_lock(&global_config.lock);
    global_config.splitters.insert(global_config.splitters.end(),
                                   ctx->local_samples.begin(), ctx->local_samples.end());
    pthread_mutex_unlock(&global_config.lock);
    
    pthread_barrier_wait(&global_config.barrier);

    // Leader worker (ID 0) selects splitters from collected samples
    if (worker_id == 0) {
        std::sort(global_config.splitters.begin(), global_config.splitters.end());
        const size_t total_samples = global_config.splitters.size();
        const size_t splitter_step = total_samples / total_workers;
        
        global_config.splitters.clear();
        for (int i = 1; i < total_workers; ++i) {
            size_t idx = i * splitter_step;
            if (idx < total_samples) {
                global_config.splitters.push_back(global_config.splitters[idx]);
            }
        }
        // Ensure exactly (num_workers - 1) splitters
        while (global_config.splitters.size() < (size_t)total_workers - 1) {
            global_config.splitters.push_back(global_config.splitters.back());
        }
        print_vector("Selected splitters", global_config.splitters, true);
    }
    pthread_barrier_wait(&global_config.barrier);

    // Phase 3: Partition and Exchange Data
    // Partition local chunk into buckets based on splitters
    std::vector<std::vector<long long>> local_buckets(total_workers);
    for (long long value : ctx->local_chunk) {
        auto split_pos = std::upper_bound(global_config.splitters.begin(),
                                          global_config.splitters.end(), value);
        int bucket_idx = std::distance(global_config.splitters.begin(), split_pos);
        bucket_idx = std::clamp(bucket_idx, 0, total_workers - 1);
        local_buckets[bucket_idx].push_back(value);
    }

    // Contribute to global buckets (thread-safe)
    for (int i = 0; i < total_workers; ++i) {
        if (!local_buckets[i].empty()) {
            pthread_mutex_lock(&global_config.bucket_locks[i]);
            global_config.bucket_contributions[i].insert(
                global_config.bucket_contributions[i].end(),
                local_buckets[i].begin(), local_buckets[i].end());
            pthread_mutex_unlock(&global_config.bucket_locks[i]);
        }
    }

    pthread_barrier_wait(&global_config.barrier);

    // Each worker takes its assigned bucket and sorts it
    ctx->local_chunk = global_config.bucket_contributions[worker_id];
    std::sort(ctx->local_chunk.begin(), ctx->local_chunk.end());

    debug_print("Worker " + std::to_string(worker_id) + 
                " final chunk size: " + std::to_string(ctx->local_chunk.size()));
    print_vector("Worker " + std::to_string(worker_id) + " final chunk", ctx->local_chunk);

    return nullptr;
}

// Main execution flow
int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] 
                  << " <seed> <workers> <imbalance> <size> [--verbose]\n";
        return 1;
    }

    // Parse command-line arguments
    global_config.random_seed = std::stoi(argv[1]);
    global_config.num_workers = std::stoi(argv[2]);
    global_config.max_imbalance = std::stod(argv[3]);
    global_config.total_elements = std::stoul(argv[4]);
    global_config.verbose_output = (argc >= 6 && std::string(argv[5]) == "--verbose");

    // Generate skewed dataset without duplicates
    std::vector<long long> unique_sequence(global_config.total_elements);
    for (size_t i = 0; i < global_config.total_elements; ++i) {
        unique_sequence[i] = i + 1; // 1 to N
    }
    // Apply skew: square each element to create a skewed distribution
    global_config.dataset.resize(global_config.total_elements);
    for (size_t i = 0; i < global_config.total_elements; ++i) {
        global_config.dataset[i] = unique_sequence[i] * unique_sequence[i];
    }
    // Shuffle the dataset to simulate random order
    std::mt19937 rng(global_config.random_seed);
    std::shuffle(global_config.dataset.begin(), global_config.dataset.end(), rng);

    if (global_config.total_elements <= 100) {
        print_vector("Full dataset before sorting", global_config.dataset, true);
    }

    // Initialize synchronization primitives
    pthread_barrier_init(&global_config.barrier, nullptr, global_config.num_workers);
    pthread_mutex_init(&global_config.lock, nullptr);
    global_config.bucket_contributions.resize(global_config.num_workers);
    global_config.bucket_locks.resize(global_config.num_workers);
    for (auto& lock : global_config.bucket_locks) {
        pthread_mutex_init(&lock, nullptr);
    }

    // Create worker threads
    std::vector<pthread_t> threads(global_config.num_workers);
    std::vector<WorkerContext> contexts(global_config.num_workers);
    for (int i = 0; i < global_config.num_workers; ++i) {
        contexts[i].worker_id = i;
        pthread_create(&threads[i], nullptr, worker_function, &contexts[i]);
    }

    // Wait for all threads to complete
    for (auto& thread : threads) pthread_join(thread, nullptr);

    // Collect and validate results
    std::vector<long long> sorted_result;
    size_t total_counted = 0;
    for (const auto& ctx : contexts) {
        sorted_result.insert(sorted_result.end(),
                             ctx.local_chunk.begin(), ctx.local_chunk.end());
        total_counted += ctx.local_chunk.size();
        debug_print("Worker " + std::to_string(ctx.worker_id) + 
                    " contributed " + std::to_string(ctx.local_chunk.size()) + 
                    " elements");
    }

    if (total_counted != global_config.total_elements) {
        std::cerr << "CRITICAL: Lost " 
                  << (global_config.total_elements - total_counted)
                  << " elements!\n";
        return 1;
    }

    // Validate sorting
    std::sort(sorted_result.begin(), sorted_result.end());
    std::vector<long long> sorted_original = global_config.dataset;
    std::sort(sorted_original.begin(), sorted_original.end());
    const bool is_valid = (sorted_result == sorted_original);
    std::cout << "Validation: " 
              << (is_valid ? "Sorted correctly!" : "Sorting failed!") 
              << "\n";
    if (global_config.verbose_output) {
        print_vector("Final sorted output", sorted_result, true);
    }

    // Cleanup synchronization primitives
    pthread_barrier_destroy(&global_config.barrier);
    pthread_mutex_destroy(&global_config.lock);
    for (auto& lock : global_config.bucket_locks) {
        pthread_mutex_destroy(&lock);
    }
    return 0;
}
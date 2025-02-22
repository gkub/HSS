#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <pthread.h>
#include <cmath>
#include <string>
#include <set>
#include <numeric>
#include <chrono> // Added for timing

// Using directives for cleaner timing code
using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double>;

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
    // Timing variables (in seconds) for each phase
    double phase1_duration;             // Initial partitioning and local sorting
    double phase2a_duration;            // Sample selection and contribution
    double phase2b_duration;            // Splitter selection (leader only)
    double phase3_duration;             // Partitioning and data exchange
    double phase4_duration;             // Final bucket sorting
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

// Worker thread function implementing the HSS algorithm with timing
void* worker_function(void* arg) {
    WorkerContext* ctx = static_cast<WorkerContext*>(arg);
    const int worker_id = ctx->worker_id;
    const size_t dataset_size = global_config.total_elements;
    const int total_workers = global_config.num_workers;

    // Phase 1: Initial Data Partitioning and Local Sorting
    auto start_phase1 = Clock::now();
    const size_t base_chunk_size = dataset_size / total_workers;
    const size_t chunk_start = worker_id * base_chunk_size;
    const size_t chunk_end = (worker_id == total_workers - 1) 
                           ? dataset_size 
                           : chunk_start + base_chunk_size;
    
    ctx->local_chunk.assign(global_config.dataset.begin() + chunk_start,
                           global_config.dataset.begin() + chunk_end);
    std::sort(ctx->local_chunk.begin(), ctx->local_chunk.end());
    auto end_phase1 = Clock::now();
    ctx->phase1_duration = Duration(end_phase1 - start_phase1).count();

    debug_print("Worker " + std::to_string(worker_id) + 
                " initial chunk size: " + std::to_string(ctx->local_chunk.size()));
    print_vector("Worker " + std::to_string(worker_id) + " initial chunk", ctx->local_chunk);

    pthread_barrier_wait(&global_config.barrier); // Barrier after Phase 1

    // Phase 2a: Sample Selection and Contribution
    auto start_phase2a = Clock::now();
    const int samples_per_worker = 10 * total_workers; // Oversampling for better splitters
    ctx->local_samples.clear();
    if (ctx->local_chunk.size() >= (size_t)samples_per_worker) {
        // Use a worker-specific seed for reproducibility
        std::mt19937 rng(global_config.random_seed + worker_id);
        std::sample(ctx->local_chunk.begin(), ctx->local_chunk.end(),
                    std::back_inserter(ctx->local_samples), samples_per_worker, rng);
    } else {
        ctx->local_samples = ctx->local_chunk;
    }

    // Contribute samples to global splitters (thread-safe)
    pthread_mutex_lock(&global_config.lock);
    global_config.splitters.insert(global_config.splitters.end(),
                                   ctx->local_samples.begin(), ctx->local_samples.end());
    pthread_mutex_unlock(&global_config.lock);
    auto end_phase2a = Clock::now();
    ctx->phase2a_duration = Duration(end_phase2a - start_phase2a).count();
    
    pthread_barrier_wait(&global_config.barrier); // Barrier after sample contribution

    // Phase 2b: Splitter Selection by Leader
    auto start_phase2b = Clock::now();
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
        while (global_config.splitters.size() < (size_t)total_workers - 1) {
            global_config.splitters.push_back(global_config.splitters.back());
        }
        print_vector("Selected splitters", global_config.splitters, true);
    }
    auto end_phase2b = Clock::now();
    ctx->phase2b_duration = (worker_id == 0) ? Duration(end_phase2b - start_phase2b).count() : 0.0;

    pthread_barrier_wait(&global_config.barrier); // Barrier after splitter selection

    // Phase 3: Partition and Exchange Data
    auto start_phase3 = Clock::now();
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
    auto end_phase3 = Clock::now();
    ctx->phase3_duration = Duration(end_phase3 - start_phase3).count();

    pthread_barrier_wait(&global_config.barrier); // Barrier after data exchange

    // Phase 4: Final Sorting of Assigned Bucket
    auto start_phase4 = Clock::now();
    ctx->local_chunk = global_config.bucket_contributions[worker_id];
    std::sort(ctx->local_chunk.begin(), ctx->local_chunk.end());
    auto end_phase4 = Clock::now();
    ctx->phase4_duration = Duration(end_phase4 - start_phase4).count();

    debug_print("Worker " + std::to_string(worker_id) + 
                " final chunk size: " + std::to_string(ctx->local_chunk.size()));
    print_vector("Worker " + std::to_string(worker_id) + " final chunk", ctx->local_chunk);

    return nullptr;
}

// Main execution flow with total timing
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
    global_config.dataset.resize(global_config.total_elements);
    for (size_t i = 0; i < global_config.total_elements; ++i) {
        global_config.dataset[i] = unique_sequence[i] * unique_sequence[i];
    }
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

    // Create worker threads and start total timing
    auto total_start = Clock::now();
    std::vector<pthread_t> threads(global_config.num_workers);
    std::vector<WorkerContext> contexts(global_config.num_workers);
    for (int i = 0; i < global_config.num_workers; ++i) {
        contexts[i].worker_id = i;
        contexts[i].phase1_duration = 0.0;  // Initialize timing variables
        contexts[i].phase2a_duration = 0.0;
        contexts[i].phase2b_duration = 0.0;
        contexts[i].phase3_duration = 0.0;
        contexts[i].phase4_duration = 0.0;
        pthread_create(&threads[i], nullptr, worker_function, &contexts[i]);
    }

    // Wait for all threads to complete
    for (auto& thread : threads) pthread_join(thread, nullptr);
    auto total_end = Clock::now();
    double total_time = Duration(total_end - total_start).count();

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

    // Compute and display timing results
    double max_phase1 = 0.0;
    double max_phase2a = 0.0;
    double leader_phase2b = 0.0;
    double max_phase3 = 0.0;
    double max_phase4 = 0.0;

    for (const auto& ctx : contexts) {
        max_phase1 = std::max(max_phase1, ctx.phase1_duration);
        max_phase2a = std::max(max_phase2a, ctx.phase2a_duration);
        if (ctx.worker_id == 0) {
            leader_phase2b = ctx.phase2b_duration;
        }
        max_phase3 = std::max(max_phase3, ctx.phase3_duration);
        max_phase4 = std::max(max_phase4, ctx.phase4_duration);
    }

    double total_phase2 = max_phase2a + leader_phase2b;
    double estimated_total = max_phase1 + total_phase2 + max_phase3 + max_phase4;

    std::cout << "\nTiming Results:\n";
    std::cout << "Phase 1 (Initial Partitioning and Sorting): " << max_phase1 << " seconds\n";
    std::cout << "Phase 2 (Splitter Selection): " << total_phase2 << " seconds\n";
    std::cout << "  - Sample Contribution: " << max_phase2a << " seconds\n";
    std::cout << "  - Splitter Selection by Leader: " << leader_phase2b << " seconds\n";
    std::cout << "Phase 3 (Partition and Exchange): " << max_phase3 << " seconds\n";
    std::cout << "Phase 4 (Final Sorting): " << max_phase4 << " seconds\n";
    std::cout << "Estimated Total Sorting Time (sum of phases): " << estimated_total << " seconds\n";
    std::cout << "Measured Total Time: " << total_time << " seconds\n";

    // Cleanup synchronization primitives
    pthread_barrier_destroy(&global_config.barrier);
    pthread_mutex_destroy(&global_config.lock);
    for (auto& lock : global_config.bucket_locks) {
        pthread_mutex_destroy(&lock);
    }
    return 0;
}
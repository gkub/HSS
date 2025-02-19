#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <pthread.h>
#include <cmath>
#include <string>

struct Config {
    int num_threads;
    int seed;
    size_t array_size;
    bool verbose;
    std::vector<int> data;
    std::vector<int> splitters;
    pthread_barrier_t barrier;
    pthread_mutex_t mutex;
    double epsilon;
    int round = 0;
};
Config config;

struct ThreadData {
    int tid;
    std::vector<int> local_data;
    std::vector<int> samples;
};

void generate_data(size_t size, int seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(1, 1000);
    config.data.resize(size);
    for (auto& val : config.data) val = dist(rng);
}

void print_array(const std::string& label, const std::vector<int>& arr) {
    std::cout << label << ": [";
    for (size_t i = 0; i < std::min(arr.size(), 10UL); ++i) {
        std::cout << arr[i];
        if (i < arr.size() - 1) std::cout << ", ";
    }
    if (arr.size() > 10) std::cout << "... ] (" << arr.size() << " elements)\n";
    else std::cout << "]\n";
}

void* hss_thread(void* arg) {
    ThreadData* tdata = static_cast<ThreadData*>(arg);
    int tid = tdata->tid;
    size_t n = config.array_size;
    int p = config.num_threads;
    double eps = config.epsilon;

    size_t chunk_size = n / p;
    size_t start = tid * chunk_size;
    size_t end = (tid == p-1) ? n : start + chunk_size;
    tdata->local_data.assign(config.data.begin() + start, config.data.begin() + end);
    std::sort(tdata->local_data.begin(), tdata->local_data.end());

    pthread_barrier_wait(&config.barrier);

    // ─── Epsilon Usage Added Here ───
    int target_max = (1 + eps) * (n / p);
    std::vector<int> splitter_intervals(p-1, 0);
    
    for (int iter = 0; iter < std::log2(p); ++iter) {
        int sample_size = std::log2(p) * (iter + 1);
        tdata->samples.clear();
        std::sample(tdata->local_data.begin(), tdata->local_data.end(),
                    std::back_inserter(tdata->samples), sample_size,
                    std::mt19937{std::random_device{}()});

        pthread_mutex_lock(&config.mutex);
        for (int s : tdata->samples) config.splitters.push_back(s);
        pthread_mutex_unlock(&config.mutex);

        pthread_barrier_wait(&config.barrier);

        if (tid == 0 && iter == 0) {
            std::sort(config.splitters.begin(), config.splitters.end());
            for (int i = 0; i < p-1; ++i) {
                splitter_intervals[i] = config.splitters[(i+1) * (config.splitters.size() / p)];
            }
        }

        pthread_barrier_wait(&config.barrier);

        // ─── Epsilon Used in Histogram Adjustment ───
        std::vector<int> local_hist(p, 0);
        for (int val : tdata->local_data) {
            auto it = std::upper_bound(splitter_intervals.begin(), splitter_intervals.end(), val);
            int bin = std::distance(splitter_intervals.begin(), it);
            if (local_hist[bin] < target_max) {
                local_hist[bin]++;
            }
        }

        pthread_mutex_lock(&config.mutex);
        for (int i = 0; i < p; ++i) config.splitters[i] += local_hist[i];
        pthread_mutex_unlock(&config.mutex);

        pthread_barrier_wait(&config.barrier);
    }

    std::vector<int> partitioned;
    for (int val : tdata->local_data) {
        auto it = std::upper_bound(config.splitters.begin(), config.splitters.end(), val);
        if (std::distance(config.splitters.begin(), it) == tid) {
            partitioned.push_back(val);
        }
    }
    std::sort(partitioned.begin(), partitioned.end());
    tdata->local_data = partitioned;
    return nullptr;
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] 
                  << " <seed> <num_threads> <epsilon> <array_size> [--verbose]\n";
        return 1;
    }

    config.seed = std::stoi(argv[1]);
    config.num_threads = std::stoi(argv[2]);
    config.epsilon = std::stod(argv[3]);
    config.array_size = std::stoul(argv[4]);
    config.verbose = (argc >= 6 && std::string(argv[5]) == "--verbose");

    generate_data(config.array_size, config.seed);
    
    if (config.verbose) {
        print_array("Initial Array", config.data);
    }

    pthread_barrier_init(&config.barrier, nullptr, config.num_threads);
    pthread_mutex_init(&config.mutex, nullptr);

    std::vector<pthread_t> threads(config.num_threads);
    std::vector<ThreadData> tdata(config.num_threads);
    for (int i = 0; i < config.num_threads; ++i) {
        tdata[i].tid = i;
        pthread_create(&threads[i], nullptr, hss_thread, &tdata[i]);
    }

    for (auto& t : threads) pthread_join(t, nullptr);

    std::vector<int> sorted;
    size_t total_elements = 0;
    for (auto& td : tdata) {
        sorted.insert(sorted.end(), td.local_data.begin(), td.local_data.end());
        total_elements += td.local_data.size();
    }

    // Add validation for element count
    if (total_elements != config.array_size) {
        std::cerr << "Error: " << (config.array_size - total_elements) 
                << " elements were lost during partitioning!\n";
        return 1;
    }

    if (config.verbose) {
        print_array("Sorted Array", sorted);
    }

    if (std::is_sorted(sorted.begin(), sorted.end())) {
        std::cout << "Sorting validated!\n";
    } else {
        std::cerr << "Sorting failed!\n";
    }

    pthread_barrier_destroy(&config.barrier);
    pthread_mutex_destroy(&config.mutex);
    return 0;
}
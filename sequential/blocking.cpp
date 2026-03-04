#include <iostream>
#include <string>
#include <queue>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <stdexcept>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#include <condition_variable>
#include "rapidjson/error/error.h"

struct ParseException : std::runtime_error, rapidjson::ParseResult
{
    ParseException(rapidjson::ParseErrorCode code, const char *msg, size_t offset) : std::runtime_error(msg),
                                                                                     rapidjson::ParseResult(code, offset) {}
};

#define RAPIDJSON_PARSE_ERROR_NORETURN(code, offset) \
    throw ParseException(code, #code, offset)

#include "rapidjson/reader.h"
#include <rapidjson/document.h>
#include <chrono>

bool debug = false;

// Updated service URL
const std::string SERVICE_URL = "http://hollywood-graph-crawler.bridgesuncc.org/neighbors/";

// Function to HTTP ecnode parts of URLs. for instance, replace spaces with '%20' for URLs
std::string url_encode(CURL *curl, std::string input)
{
    char *out = curl_easy_escape(curl, input.c_str(), input.size());
    std::string s = out;
    curl_free(out);
    return s;
}

// Callback function for writing response data
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *output)
{
    size_t totalSize = size * nmemb;
    output->append((char *)contents, totalSize);
    return totalSize;
}

// Function to fetch neighbors using libcurl with debugging
std::string fetch_neighbors(CURL *curl, const std::string &node)
{

    std::string url = SERVICE_URL + url_encode(curl, node);
    std::string response;

    if (debug)
        std::cout << "Sending request to: " << url << std::endl;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // Verbose Logging

    // Set a User-Agent header to avoid potential blocking by the server
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: C++-Client/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK)
    {
        std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
    }
    else
    {
        if (debug)
            std::cout << "CURL request successful!" << std::endl;
    }

    // Cleanup
    curl_slist_free_all(headers);

    if (debug)
        std::cout << "Response received: " << response << std::endl; // Debug log

    return (res == CURLE_OK) ? response : "{}";
}

// Function to parse JSON and extract neighbors
std::vector<std::string> get_neighbors(const std::string &json_str)
{
    std::vector<std::string> neighbors;
    try
    {
        rapidjson::Document doc;
        doc.Parse(json_str.c_str());

        if (doc.HasMember("neighbors") && doc["neighbors"].IsArray())
        {
            for (const auto &neighbor : doc["neighbors"].GetArray())
                neighbors.push_back(neighbor.GetString());
        }
    }
    catch (const ParseException &e)
    {
        std::cerr << "Error while parsing JSON: " << json_str << std::endl;
        throw e;
    }
    return neighbors;
}

class BlockingQueue
{
    std::queue<std::pair<std::string, int>> queue;
    std::mutex mtx;
    std::condition_variable cv;
    bool finished = false;

public:
    void push(std::pair<std::string, int> item)
    {
        std::lock_guard<std::mutex> lock(mtx);
        queue.push(item);
        cv.notify_one();
    }

    bool pop(std::pair<std::string, int> &item)
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]
                { return !queue.empty() || finished; });
        if (!queue.empty())
        {
            item = queue.front();
            queue.pop();
            return true;
        }
        return false;
    }

    void finish()
    {
        std::lock_guard<std::mutex> lock(mtx);
        finished = true;
        cv.notify_all();
    }
};

// BFS Traversal Function
std::vector<std::string> bfs(const std::string &start, int depth)
{
    BlockingQueue queue;
    std::mutex mtx;
    std::mutex visited_mutex;
    std::unordered_set<std::string> visited;
    std::vector<std::string> result;
    int active_threads = 0;
    std::condition_variable cv;

    queue.push({start, 0});
    visited.insert(start);

    int num_threads = 8;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; i++)
    {
        threads.push_back(std::thread([&]()
                                      {
            CURL *tcurl = curl_easy_init();
            std::pair<std::string, int> item;

            while (queue.pop(item))
            {

                {
                    std::lock_guard<std::mutex> lock(mtx);
                    active_threads++;
                }

                auto [node, level] = item;

                {
                    std::lock_guard<std::mutex> lock(visited_mutex);
                    result.push_back(node);
                }

                if (level < depth)
                {
                    for (const auto &neighbor : get_neighbors(fetch_neighbors(tcurl, node)))
                    {
                        std::lock_guard<std::mutex> lock(visited_mutex);
                        if (!visited.count(neighbor))
                        {
                            visited.insert(neighbor);
                            queue.push({neighbor, level + 1});
                        }
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(mtx);
                    active_threads--;
                    if (active_threads == 0)
                    {
                        queue.finish();
                    }
                }
            }
            curl_easy_cleanup(tcurl); 
        }));
    }

    for (auto &t : threads) {
        t.join();
    }

    return result;

}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <node_name> <depth>\n";
        return 1;
    }

    std::string start_node = argv[1]; // example "Tom%20Hanks"
    int depth;
    try
    {
        depth = std::stoi(argv[2]);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: Depth must be an integer.\n";
        return 1;
    }

    /*
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        std::cerr << "Failed to initialize CURL\n";
        return -1;
    }
    */

    const auto start{std::chrono::steady_clock::now()};

    std::ofstream logFile("blocking_log.txt", std::ios::app);
    if (!logFile)
    {
        std::cerr << "Error opening log file." << std::endl;
        return 1;
    }
    for (const auto &n : bfs(start_node, depth))
    {
        logFile << "- " << n << "\n";
    }

    const auto finish{std::chrono::steady_clock::now()};
    const std::chrono::duration<double> elapsed_seconds{finish - start};
    std::cout << "Time to crawl: " << elapsed_seconds.count() << "s\n";
    logFile << "With Blocking Queue: Crawled " << start_node << " to depth " << depth << " in " << elapsed_seconds.count() << " seconds.\n"
            << std::endl;
    logFile.close();

    //curl_easy_cleanup(curl);
    return 0;
}

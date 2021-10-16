#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <assert.h>
#include <thread>
#include <future>
#include <csignal>
#include <atomic>
#include <variant>

// System (platform specific) includes
#include <sys/inotify.h>
#include <sys/fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <poll.h>

// Graphics and other includes
#include "SFML/Graphics.hpp"
#include "utils.h"

// Max buffer length
static const size_t BUF_LEN(10 * (sizeof(struct inotify_event) + NAME_MAX + 1));
static sf::Vector2u winSize(800, 800);

// A file that is watched for changes and hot-reloaded for live viewing
static std::filesystem::path watchedFile;
static int inot_instance, watch_handle;

// Set this to true to make the file watcher thread exit
static std::atomic<bool> watcherShouldExit = false;

struct shaderinfo {
    sf::Shader* shader;
    std::filesystem::path path;
};

struct channelinfo {
    std::variant<sf::Shader*, sf::Texture*> input;
    std::filesystem::path path;

    // Flags
    bool autoReload : 1;
}

// Maps watch handle to shader pointer
// TODO: Mutex/Semaphore
auto shaderWatches = std::map<int, shaderinfo>();


// Done: Use poll() instead of read and make the thread exit properly
void shaderWatcherThread() {
    ssize_t numRead;
    char* p;
    inotify_event* event;
    char buf[BUF_LEN] __attribute__((aligned(8)));
    // fcntl(watch_handle, O_NONBLOCK); // Prevent the file handle from blocking

    // Info for polling
    struct pollfd pd = {
        inot_instance,  // File descriptor to poll
        POLLIN,         // Event mask to poll for
        0               // Return value
    };

    // Poll for thread exit request
    while (!watcherShouldExit) {
        if (!poll(&pd, 1, 10)) continue;

        numRead = read(inot_instance, buf, BUF_LEN);
        if (numRead == 0)
            continue;
        if (numRead == -1)
            return;
        std::cout << "Looping" << std::endl;
        /* Process all of the events in buffer returned by read() */
        for (p = buf; p < buf + numRead;) {
            event = (struct inotify_event*)p;
            std::cout << "File " << event->wd << " changed... Reloading" << std::endl;
            // displayInotifyEvent(event);

            // Load the shader changed from the map of shader information
            if (shaderWatches.contains(event->wd))
                shaderWatches[event->wd].shader->loadFromFile(shaderWatches[event->wd].path.string(), sf::Shader::Fragment);
            p += sizeof(inotify_event) + event->len;
        }
    }
}

/// Add a shader change watch that can be monitored by the worker thread, 
/// @returns the inotify handle for the watch
int addShaderWatch(std::filesystem::path file, sf::Shader* shader) {
    int handle = inotify_add_watch(inot_instance, file.string().c_str(), IN_CLOSE_WRITE); // Get a watcher handle
    struct shaderinfo si = { shader, file }; // Make a pair of the shader and file
    shaderWatches.emplace(handle, si);  // Add it to the map for access by the thread

    return handle;
}

/// Handle termination signals to "gracefully die"
void signalHandler(int sig) {
    watcherShouldExit = true;
    exit(sig);
}

int main(int argc, char const* argv[]) {
    if (argc < 2) {
        std::cout << "Error, at least a single argument specifying fragment shader filename must be supplied." << std::endl
            << "Additional arguments can be supplied to provide shaders or textures for additional channels." << std::endl;
        return 1;
    }

    watchedFile = std::filesystem::absolute(
        std::filesystem::path(argv[1])
    );

    auto iChannels = std::vector<sf::Texture>();
    for (int ti = 2; ti < argc; ++ti) {
        if (std::filesystem::exists(argv[ti])) {
            sf::Texture tex;
            tex.loadFromFile(argv[ti]);
            tex.generateMipmap();
            iChannels.push_back(tex);
        }
    }

    // Watch for termination signal
    signal(SIGINT, signalHandler);

    // Setup inotify
    inot_instance = inotify_init();
    assert(inot_instance != -1);
    //watch_handle = inotify_add_watch(inot_instance, watchedFile.string().c_str(), IN_CLOSE_WRITE);
    //std::cout << "Watch handle is " << watch_handle << std::endl;

    sf::RenderWindow window = sf::RenderWindow(sf::VideoMode(winSize.x, winSize.y), "Simple shader display");
    window.setView(sf::View(sf::FloatRect(0.0, 0.0, 1.0, 1.0)));

    // Main shader for the program
    sf::Shader mainShader = sf::Shader();
    mainShader.loadFromFile(watchedFile.string(), sf::Shader::Fragment);

    // This thread polls for inotify events and in turn, watches for file changes
    std::thread watcherThread = std::thread(&shaderWatcherThread);
    addShaderWatch(watchedFile, &mainShader);


    // Fullscreen quad
    sf::RectangleShape rect = sf::RectangleShape(sf::Vector2f(winSize));

    /*
     * Shader Inputs
     * uniform vec3      iResolution;           // viewport resolution (in pixels)
     * uniform float     iTime;                 // shader playback time (in seconds)
     * uniform float     iTimeDelta;            // render time (in seconds)
     * uniform int       iFrame;                // shader playback frame
     * uniform float     iChannelTime[4];       // channel playback time (in seconds)
     * uniform vec3      iChannelResolution[4]; // channel resolution (in pixels)
     * uniform vec4      iMouse;                // mouse pixel coords. xy: current (if MLB down), zw: click
     * uniform samplerXX iChannel0..3;          // input channel. XX = 2D/Cube
     * uniform vec4      iDate;                 // (year, month, day, time in seconds)
     * uniform float     iSampleRate;           // sound sample rate (i.e., 44100)
    */


    // Frame count and deltaTime
    int frameCounter = 0;
    sf::Clock deltaClock;
    sf::Clock appClock;

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();
            if (event.type == sf::Event::Resized) {
                winSize = sf::Vector2u(event.size.width, event.size.height);
                mainShader.setUniform("iResolution", sf::Vector2f(winSize));
            }
        }

        mainShader.setUniform("iResolution", sf::Vector2f(winSize));
        mainShader.setUniform("iFrame", frameCounter++);
        mainShader.setUniform("iTimeDelta", deltaClock.restart().asSeconds());
        mainShader.setUniform("iTime", appClock.getElapsedTime().asSeconds());

        for (int i = 0; i < iChannels.size(); ++i) {
            mainShader.setUniform("iChannel" + std::to_string(i), iChannels[i]);
        }

        // Clear color
        window.clear(sf::Color::White);

        window.draw(rect, &mainShader);

        // Update framebuffer
        window.display();
    }

    // Done: Fix the thread function(use poll())
    // pthread_cancel(watcherThread.native_handle());

    // Signal the thread to exit and join it
    watcherShouldExit = true;
    watcherThread.join();

    return 0;
}

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

// SFML Window size (iResolution)
static sf::Vector2u winSize(800, 800);

// INotify file handle
static int inot_instance;

// Set this to true to make the file watcher thread exit
static std::atomic<bool> watcherShouldExit = false;

typedef std::shared_ptr<sf::Shader> ShaderResource;
typedef std::shared_ptr<sf::Texture> TextureResource;
typedef std::variant<ShaderResource, TextureResource> Resource;
//typedef sf::Shader* Resource;

struct channelinfo {
    Resource input;
    std::filesystem::path path;

    // Flags
    bool autoReload : 1 = true;
};

// Maps watch handle to shader pointer
// TODO: Mutex/Semaphore
// auto shaderWatches = std::map<int, shaderinfo>();
auto reloadWatches = std::map<int, channelinfo*>(); // Points to the channelinfo in a vector of channels
static std::filesystem::path mainShaderFile;

// DONE: Use poll() instead of read and make the thread exit properly
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



            // Load the shader changed from the map of shader information, check if its meant to be reloaded
            if (reloadWatches.contains(event->wd) && reloadWatches[event->wd]->autoReload) {
                // Contains a shader
                switch (reloadWatches[event->wd]->input.index()) {
                    case 0:
                        // Load the shader from file
                        std::get<ShaderResource>(reloadWatches[event->wd]->input)
                            ->loadFromFile(reloadWatches[event->wd]->path.string(), sf::Shader::Fragment);
                        break;

                    case 1:
                        std::get<TextureResource>(reloadWatches[event->wd]->input)
                            ->loadFromFile(reloadWatches[event->wd]->path.string());
                        break;

                    default: break;
                }
            }


            //.shader->loadFromFile(shaderWatches[event->wd].path.string(), sf::Shader::Fragment);
            p += sizeof(inotify_event) + event->len;
        }
    }
}



/// Add a shader change watch that can be monitored by the worker thread, 
/// @returns the inotify handle for the watch
int addResourceWatch(std::filesystem::path file, Resource r) {
    int handle = inotify_add_watch(inot_instance, file.string().c_str(), IN_CLOSE_WRITE); // Get a watcher handle
    channelinfo ci = { r, file }; // Make a pair of the shader and file
    reloadWatches.emplace(handle, &ci);  // Add it to the map for access by the thread

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

    mainShaderFile = std::filesystem::absolute(
        std::filesystem::path(argv[1])
    );

    auto iChannels = std::vector<channelinfo>();
    for (int ti = 2; ti < argc; ++ti) {
        if (std::filesystem::exists(argv[ti])) {
            TextureResource tex = std::make_shared<sf::Texture>();
            tex->loadFromFile(argv[ti]);
            tex->generateMipmap();
            struct channelinfo ci = {
                tex,
                std::filesystem::path(argv[ti]),
                true
            };
            iChannels.push_back(ci);
        }
    }

    // Watch for termination signal
    signal(SIGINT, signalHandler);

    // Setup inotify
    inot_instance = inotify_init();
    assert(inot_instance != -1);
    //watch_handle = inotify_add_watch(inot_instance, mainShaderFile.string().c_str(), IN_CLOSE_WRITE);
    //std::cout << "Watch handle is " << watch_handle << std::endl;

    sf::RenderWindow window = sf::RenderWindow(sf::VideoMode(winSize.x, winSize.y), "Simple shader display");
    window.setView(sf::View(sf::FloatRect(0.0, 0.0, 1.0, 1.0)));

    // Main shader for the program
    ShaderResource mainShader = std::make_shared<sf::Shader>(); // = sf::Shader();
    mainShader->loadFromFile(mainShaderFile.string(), sf::Shader::Fragment);

    // This thread polls for inotify events and in turn, watches for file changes
    std::thread watcherThread = std::thread(&shaderWatcherThread);
    addResourceWatch(mainShaderFile, mainShader);

    // Fullscreen quad
    sf::RectangleShape rect = sf::RectangleShape(sf::Vector2f(winSize));

    auto channel_textures = std::vector<sf::RenderTexture>();

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

    // Main render loop
    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();
            if (event.type == sf::Event::Resized) {
                winSize = sf::Vector2u(event.size.width, event.size.height);
                mainShader->setUniform("iResolution", sf::Vector2f(winSize));
            }
        }

        // Set global uniforms
        mainShader->setUniform("iResolution", sf::Vector2f(winSize));
        mainShader->setUniform("iFrame", frameCounter++);
        mainShader->setUniform("iTimeDelta", deltaClock.restart().asSeconds());
        mainShader->setUniform("iTime", appClock.getElapsedTime().asSeconds());



        // Set the channel values
        for (int i = 0; i < iChannels.size(); ++i) {
            switch (iChannels[i].input.index()) {
                case 1: // Texture
                    mainShader->setUniform("iChannel" + std::to_string(i), *std::get<TextureResource>(iChannels[i].input));
                    break;
                default: break;
            }
        }

        // Clear color
        window.clear(sf::Color::White);

        // Render the fullscreen quad
        window.draw(rect, mainShader.get());

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

#pragma region includes
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
#include <X11/Xlib.h>
#include "utils.h"
#pragma endregion

// Buffer length for reading inotify events
static const size_t BUF_LEN(10 * (sizeof(struct inotify_event) + NAME_MAX + 1));

// SFML Window size (iResolution), updated when resized
static sf::Vector2u winSize(800, 800);

// INotify file handle that emits events
static int inot_instance;

// Set this to true to make the file watcher thread exit
static std::atomic<bool> watcherShouldExit = false;

typedef std::shared_ptr<sf::Shader> ShaderResource;
typedef std::shared_ptr<sf::Texture> TextureResource;
typedef std::variant<ShaderResource, TextureResource> Resource;
//typedef sf::Shader* Resource;

// Aggregates information about a resource together
struct ResInfo {
    // Actual resource pointer
    Resource input;
    // Path of the file the resource is loaded from
    std::filesystem::path path;

    // Flags
    // bool autoReload : 1 = true;
};

// TODO: Mutex/Semaphore
// Maps watch handle to shader pointer
auto reloadWatches = std::map<int, ResInfo*>(); // Points to the channelinfo in a vector of channels
// A mutex lock to prevent shader changes during a frame;
static std::mutex resourcesLock;

// DONE: Use poll() instead of read and make the thread exit properly
void shaderWatcherThread() {
    ssize_t numRead;
    char* p; // Read cursor
    inotify_event* event; // Read holder
    char buf[BUF_LEN] __attribute__((aligned(8))); // Buffer
    fcntl(inot_instance, O_NONBLOCK); // Prevent the file handle from blocking

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

        // std::cout << "Looping" << std::endl;
        /* Process all of the events in buffer returned by read() */
        for (p = buf; p < buf + numRead;) {
            event = (struct inotify_event*)p;
            std::cout << "File " << reloadWatches[event->wd]->path.string() << " changed... Reloading" << std::endl;
            // displayInotifyEvent(event);

            // Load the shader changed from the map of shader information, check if its meant to be reloaded
            if (reloadWatches.contains(event->wd)) {
                resourcesLock.lock();
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
                resourcesLock.unlock();
            }

            //.shader->loadFromFile(shaderWatches[event->wd].path.string(), sf::Shader::Fragment);
            // Seek to the next event
            p += sizeof(inotify_event) + event->len;
        }
    }
}

/// Add a resource change watch that can be monitored by the worker thread,
/// @returns the inotify handle for the watch
int addResourceWatch(std::filesystem::path file, Resource r) {
    int handle = inotify_add_watch(inot_instance, file.c_str(), IN_CLOSE_WRITE); // Get a watcher handle
    assert(handle != -1);
    std::cout << "New watch handle: " << std::to_string(handle) << std::endl;

    ResInfo* fileInfo = new ResInfo();
    fileInfo->input = r;
    fileInfo->path = file; // Make a pair of the shader and file
    reloadWatches.emplace(handle, fileInfo);  // Add it to the map for access by the thread

    return handle;
}

/// Add a resource change watch that can be monitored by the worker thread,
/// @returns the inotify handle for the watch
int addResourceWatch(ResInfo* r) {
    int handle = inotify_add_watch(inot_instance, std::filesystem::absolute(r->path).c_str(), IN_CLOSE_WRITE); // Get a watcher handle
    std::cout << "Added resource watch to file '" << std::filesystem::absolute(r->path).string() << "'" << std::endl;
    assert(handle != -1);
    std::cout << "New watch handle: " << std::to_string(handle) << std::endl;
    reloadWatches.emplace(handle, r);  // Add it to the map for access by the thread
    return handle;
}

// /// Handle termination signals to "gracefully die"
// void signalHandler(int sig) {
//     watcherShouldExit = true;
//     exit(sig);
// }

int main(int argc, char const* argv[]) {
    if (argc < 2) {
        std::cout << "Error, at least a single argument specifying fragment shader filename must be supplied." << std::endl
            << "Additional arguments can be supplied to provide shaders or textures for additional channels." << std::endl;
        return 1;
    }

    // Path to the primary shader
    std::filesystem::path mainShaderFile = std::filesystem::path(argv[1]);

    // Watch for termination signal
    // signal(SIGINT, signalHandler);
    XInitThreads();

    // Setup inotify
    inot_instance = inotify_init();
    assert(inot_instance != -1);

    // Initialize the SFML window
    sf::RenderWindow window = sf::RenderWindow(sf::VideoMode(winSize.x, winSize.y), "Simple shader display");
    window.setView(sf::View(sf::FloatRect(0.0, 0.0, 1.0, 1.0)));

    // Information about the resources used 
    auto iChannels = std::vector<ResInfo>();
    auto iRenderBuffers = std::vector<std::shared_ptr<sf::RenderTexture>>();

    for (int i = 0; i < argc - 2; ++i) {
        ResInfo resInfo;
        std::filesystem::path pth = std::filesystem::path(argv[i + 2]);

        if (!std::filesystem::exists(pth)) {
            std::cerr << "Error loading file " << pth.string() << std::endl;
            continue;
        }

        if (pth.string().ends_with(".glsl")) {
            // Instantiate and create the shader
            ShaderResource shader = std::make_shared<sf::Shader>();
            shader->loadFromFile(pth.string(), sf::Shader::Fragment);

            std::cout << "Loading shader channel " << pth.string() << std::endl;

            resInfo = {
                shader,
                pth
            };

            // Render texture to render into
            auto rtex = std::make_shared<sf::RenderTexture>();
            rtex->create(winSize.x, winSize.y);
            iRenderBuffers.push_back(rtex);
        }
        else {
            TextureResource tex = std::make_shared<sf::Texture>();
            tex->loadFromFile(pth.string());

            resInfo = {
                tex,
                pth
            };

            iRenderBuffers.push_back(std::shared_ptr<sf::RenderTexture>(nullptr));
        }

        iChannels.push_back(resInfo);
        // Watch the resource
        addResourceWatch(&iChannels[i]);
    }

    // Primary (display) shader
    ShaderResource mainShaderRef = std::make_shared<sf::Shader>();
    mainShaderRef->loadFromFile(mainShaderFile.string(), sf::Shader::Fragment);
    ResInfo mainShaderInfo = {
        mainShaderRef,
        mainShaderFile
    };
    addResourceWatch(&mainShaderInfo);

    // This thread polls for inotify events and in turn, watches for file changes
    std::thread watcherThread = std::thread(&shaderWatcherThread);

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

    // Main render loop
    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();
            if (event.type == sf::Event::Resized) {
                winSize = sf::Vector2u(event.size.width, event.size.height);

                // Resize the render textures
                for (auto rtex : iRenderBuffers) {
                    if (rtex.get() != nullptr)
                        rtex->create(winSize.x, winSize.y);
                }
            }
        }

        // Lock the shaders
        resourcesLock.lock();

        // Set global uniforms
        mainShaderRef->setUniform("iResolution", sf::Vector2f(winSize));
        mainShaderRef->setUniform("iFrame", frameCounter++);
        mainShaderRef->setUniform("iTimeDelta", deltaClock.restart().asSeconds());
        mainShaderRef->setUniform("iTime", appClock.getElapsedTime().asSeconds());
        mainShaderRef->setUniform("iMouse", sf::Vector2f(sf::Mouse::getPosition(window)));

        // Set the channel values
        // If the channel is a shader render them into the render textures
        for (int i = 0; i < iChannels.size(); ++i) {
            switch (iChannels[i].input.index()) {
                case 0: { // Shader
                    sf::Shader* channelShader = std::get<ShaderResource>(iChannels[i].input).get();
                    assert(channelShader->isAvailable());

                    // Set the textures as like feedback
                    for (int rti = 0; rti < iRenderBuffers.size(); ++rti)
                        channelShader->setUniform("iChannel" + std::to_string(i), iRenderBuffers[i]->getTexture());

                    // Set uniforms into the subshader
                    channelShader->setUniform("iResolution", sf::Vector2f(winSize));
                    channelShader->setUniform("iFrame", frameCounter++);
                    channelShader->setUniform("iTimeDelta", deltaClock.restart().asSeconds());
                    channelShader->setUniform("iTime", appClock.getElapsedTime().asSeconds());
                    channelShader->setUniform("iMouse", sf::Mouse::getPosition(window));

                    // Setup the context
                    iRenderBuffers[i]->clear(sf::Color::Black);

                    // States to render without alpha blending
                    sf::RenderStates states(channelShader);
                    states.blendMode = sf::BlendNone;

                    iRenderBuffers[i]->setView(sf::View(sf::FloatRect(0.0, 0.0, 1.0, 1.0)));
                    iRenderBuffers[i]->draw(rect, channelShader);

                    iRenderBuffers[i]->display();
                    // Convert to texture and send to the shader
                    mainShaderRef->setUniform("iChannel" + std::to_string(i), iRenderBuffers[i]->getTexture());
                } break;

                case 1: // Texture
                    mainShaderRef->setUniform("iChannel" + std::to_string(i), *std::get<TextureResource>(iChannels[i].input));
                    break;

                default: return 1;
            }
        }

        // Clear color
        window.clear(sf::Color::Black);

        // States to render without alpha blending
        sf::RenderStates states(mainShaderRef.get());
        states.blendMode = sf::BlendNone;
        // Render the fullscreen quad
        window.draw(rect, states);

        // Unlock the shaders
        resourcesLock.unlock();

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
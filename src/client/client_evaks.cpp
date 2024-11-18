// client_evaks.cpp

#include "api.h"
#include "utils.h"
#include <SFML/System.hpp>
#include <iostream>
#include <string>
#include <unordered_set>
#include <queue>
#include <spdlog/spdlog.h>

// Define a hash function for sf::Vector2i to use in unordered_set
struct Vector2iHash {
    std::size_t operator()(const sf::Vector2i& vec) const {
        return std::hash<int>()(vec.x) ^ (std::hash<int>()(vec.y) << 1);
    }
};

class ELSbot {
private:
    cycles::Connection connection;                        // Connection to the server
    std::string name;                                     // Bot's name
    cycles::GameState state;                              // Current game state
    cycles::Player myPlayer;                              // This bot's player information
    bool movingDown = true;                               // Track zigzag direction
    cycles::Direction primaryDirection = cycles::Direction::east; // Main movement direction set to east

    // Trail tracking to prevent self-collision, implemented becayuse it kept on running into itself
    std::unordered_set<sf::Vector2i, Vector2iHash> trail; // Quick lookup for occupied positions using unordered_set
    std::queue<sf::Vector2i> trailQueue;                  // Maintains the order of visited positions
    const size_t MAX_TRAIL_SIZE = 5000;                   // Maximum number of positions to track

    // Convert Direction enum to sf::Vector2i
    sf::Vector2i getDirectionVector(cycles::Direction direction) const {
        switch(direction) {
            case cycles::Direction::north: return sf::Vector2i(0, -1);
            case cycles::Direction::south: return sf::Vector2i(0, 1);
            case cycles::Direction::east:  return sf::Vector2i(1, 0);
            case cycles::Direction::west:  return sf::Vector2i(-1, 0);
            default:                        return sf::Vector2i(1, 0); // Default to EAST
        }
    }

    // Convert Direction enum to string for logging
    std::string directionToString(cycles::Direction dir) const {
        switch(dir) {
            case cycles::Direction::north: return "NORTH";
            case cycles::Direction::south: return "SOUTH";
            case cycles::Direction::east:  return "EAST";
            case cycles::Direction::west:  return "WEST";
            default:                        return "EAST"; // Default fallback
        }
    }

    // Check if a move is valid
    bool isValidMove(cycles::Direction direction) {
        sf::Vector2i newPos = myPlayer.position + getDirectionVector(direction);
        return state.isInsideGrid(newPos) && state.isCellEmpty(newPos) && trail.find(newPos) == trail.end();
    }

    // Decide the next move using a safe zigzag strategy
    cycles::Direction decideMove() {
        cycles::Direction zigzagDirection = movingDown ? cycles::Direction::south : cycles::Direction::north;

        // Prioritize zigzag movement (up or down)
        if (isValidMove(zigzagDirection)) {
            return zigzagDirection;
        }

        // If zigzag direction is blocked, try the primary direction (east)
        if (isValidMove(primaryDirection)) {
            movingDown = !movingDown; // Reverse zigzag direction
            return primaryDirection;
        }

        // Attempt alternative directions to avoid getting stuck
        std::vector<cycles::Direction> alternativeDirections = {
            cycles::Direction::west,  // Try moving west as an alternative
            cycles::Direction::north,
            cycles::Direction::south
        };

        for (const auto& dir : alternativeDirections) {
            if (isValidMove(dir)) {
                return dir;
            }
        }

        // If no valid moves are available, attempt to stay in place or choose any possible move
        spdlog::warn("{}: No valid moves available. Staying put.", name);
        return primaryDirection; // Fallback to primary direction
    }

    // Update the bot's state with the current game state
    void updateState() {
        state = connection.receiveGameState();
        for (const auto& player : state.players) {
            if (player.name == name) {
                myPlayer = player;
                break;
            }
        }
        spdlog::debug("{}: Updated position to ({}, {})", name, myPlayer.position.x, myPlayer.position.y);
    }

    // Send the decided move to the server and update the trail
    void sendMove() {
        cycles::Direction move = decideMove();
        connection.sendMove(move); // sendMove returns void
        spdlog::info("{}: Sent move {}", name, directionToString(move));

        // Update the trail with the new position
        sf::Vector2i newPos = myPlayer.position + getDirectionVector(move);
        trail.insert(newPos);
        trailQueue.push(newPos);

        // Maintain trail size by removing the oldest positions
        if (trailQueue.size() > MAX_TRAIL_SIZE) {
            sf::Vector2i oldPos = trailQueue.front();
            trailQueue.pop();
            trail.erase(oldPos);
        }

        // Check if connection is still active
        if (!connection.isActive()) {
            spdlog::error("{}: Connection lost. Exiting.", name);
            exit(1);
        }
    }

public:
    // Constructor: Initializes the bot and connects to the server
    ELSbot(const std::string& botName) : name(botName) {
        // Connects to sevre, if connection fails, exit else log connection success
        connection.connect(name);
        if (!connection.isActive()) {
            spdlog::critical("{}: Connection failed", name);
            exit(1);
        }
        spdlog::info("{}: Connected to server", name);
    }

    // Running in aloop while game is active
    void run() {
        while (connection.isActive()) {
            updateState();
            sendMove();
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <bot_name>" << std::endl;
        return 1;
    }

    // Set logging level based on compilation flags
#if SPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_TRACE // If trace level is enabled in spdlog 
    spdlog::set_level(spdlog::level::debug); // Set logging level to debug
#else
    spdlog::set_level(spdlog::level::info);
#endif

    std::string botName = argv[1];
    ELSbot bot(botName);
    bot.run();
    return 0;
}
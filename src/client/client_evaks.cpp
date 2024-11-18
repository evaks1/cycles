#include "api.h"
#include "utils.h"
#include <SFML/System.hpp>
#include <iostream>
#include <string>
#include <unordered_set>
#include <queue>
#include <stdexcept>
#include <spdlog/spdlog.h>

// Define a hash function for sf::Vector2i to use in unordered_set
struct Vector2iHash {
    std::size_t operator()(const sf::Vector2i& vec) const {
        return std::hash<int>()(vec.x) ^ (std::hash<int>()(vec.y) << 1);
    }
};

// Custom exception class for critical bot errors
class BotException : public std::runtime_error {
public:
    explicit BotException(const std::string& message)
        : std::runtime_error(message) {}
};

class ELSbot {
private:
    cycles::Connection connection;                        // Connection to the server
    std::string name;                                     // Bot's name
    cycles::GameState state;                              // Current game state
    cycles::Player myPlayer;                              // This bot's player information
    bool movingDown = true;                               // Track zigzag direction
    cycles::Direction primaryDirection = cycles::Direction::east; // Main movement direction set to east

    // Trail tracking to prevent self-collision
    std::unordered_set<sf::Vector2i, Vector2iHash> trail; // Quick lookup for occupied positions
    std::queue<sf::Vector2i> trailQueue;                  // Maintains the order of visited positions
    const size_t MAX_TRAIL_SIZE = 5000;                   // Maximum number of positions to track

    // Convert Direction enum to sf::Vector2i
    sf::Vector2i getDirectionVector(cycles::Direction direction) const {
        switch(direction) {
            case cycles::Direction::north: return sf::Vector2i(0, -1);
            case cycles::Direction::south: return sf::Vector2i(0, 1);
            case cycles::Direction::east:  return sf::Vector2i(1, 0);
            case cycles::Direction::west:  return sf::Vector2i(-1, 0);
            default:
                spdlog::error("{}: Invalid direction encountered", name);
                throw BotException("Invalid direction encountered");
        }
    }

    // Convert Direction enum to string for logging
    std::string directionToString(cycles::Direction dir) const {
        switch(dir) {
            case cycles::Direction::north: return "NORTH";
            case cycles::Direction::south: return "SOUTH";
            case cycles::Direction::east:  return "EAST";
            case cycles::Direction::west:  return "WEST";
            default:                        return "UNKNOWN";
        }
    }

    // Check if a move is valid
    bool isValidMove(cycles::Direction direction) {
        sf::Vector2i newPos = myPlayer.position + getDirectionVector(direction);
        if (!state.isInsideGrid(newPos)) {
            spdlog::warn("{}: Position ({}, {}) is outside the grid", name, newPos.x, newPos.y);
            return false;
        }
        if (!state.isCellEmpty(newPos)) {
            spdlog::warn("{}: Position ({}, {}) is not empty", name, newPos.x, newPos.y);
            return false;
        }
        if (trail.find(newPos) != trail.end()) {
            spdlog::warn("{}: Position ({}, {}) is part of the trail", name, newPos.x, newPos.y);
            return false;
        }
        return true;
    }

    // Decide the next move using a safe zigzag strategy
    cycles::Direction decideMove() {
        cycles::Direction zigzagDirection = movingDown ? cycles::Direction::south : cycles::Direction::north;

        try {
            // Prioritize zigzag movement (up or down)
            if (isValidMove(zigzagDirection)) {
                return zigzagDirection;
            }

            // If zigzag direction is blocked, try the primary direction (east)
            if (isValidMove(primaryDirection)) {
                movingDown = !movingDown; // Reverse zigzag direction
                return primaryDirection;
            }

            // Attempt alternative directions
            for (auto dir : {cycles::Direction::west, cycles::Direction::north, cycles::Direction::south}) {
                if (isValidMove(dir)) {
                    return dir;
                }
            }
        } catch (const BotException& e) {
            spdlog::critical("{}: Bot exception encountered: {}", name, e.what());
            throw;
        }

        spdlog::error("{}: No valid moves available. Staying put.", name);
        return primaryDirection; // Fallback direction
    }

    // Update the bot's state with the current game state
    void updateState() {
        try {
            state = connection.receiveGameState();
            bool playerFound = false;
            for (const auto& player : state.players) {
                if (player.name == name) {
                    myPlayer = player;
                    playerFound = true;
                    break;
                }
            }
            if (!playerFound) {
                throw BotException("Player not found in the game state");
            }
            spdlog::debug("{}: Updated position to ({}, {})", name, myPlayer.position.x, myPlayer.position.y);
        } catch (const std::exception& e) {
            spdlog::critical("{}: Failed to update state: {}", name, e.what());
            throw;
        }
    }

    // Send the decided move to the server and update the trail
    void sendMove() {
        try {
            cycles::Direction move = decideMove();
            connection.sendMove(move);
            spdlog::info("{}: Sent move {}", name, directionToString(move));

            // Update the trail with the new position
            sf::Vector2i newPos = myPlayer.position + getDirectionVector(move);
            trail.insert(newPos);
            trailQueue.push(newPos);

            // Maintain trail size
            if (trailQueue.size() > MAX_TRAIL_SIZE) {
                sf::Vector2i oldPos = trailQueue.front();
                trailQueue.pop();
                trail.erase(oldPos);
            }
        } catch (const std::exception& e) {
            spdlog::critical("{}: Failed to send move: {}", name, e.what());
            throw;
        }
    }

public:
    // Constructor: Initializes the bot and connects to the server
    ELSbot(const std::string& botName) : name(botName) {
        try {
            connection.connect(name);
            if (!connection.isActive()) {
                throw BotException("Connection to server failed");
            }
            spdlog::info("{}: Connected to server", name);
        } catch (const std::exception& e) {
            spdlog::critical("{}: Initialization failed: {}", name, e.what());
            throw;
        }
    }

    // Run the bot in a loop while the game is active
    void run() {
        try {
            while (connection.isActive()) {
                updateState();
                sendMove();
            }
        } catch (const std::exception& e) {
            spdlog::critical("{}: Bot encountered a critical error: {}", name, e.what());
            exit(1);
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <bot_name>" << std::endl;
        return 1;
    }

    // Set logging level
    spdlog::set_level(spdlog::level::info);

    try {
        std::string botName = argv[1];
        ELSbot bot(botName);
        bot.run();
    } catch (const std::exception& e) {
        spdlog::critical("Fatal error: {}", e.what());
        return 1;
    }

    return 0;
}

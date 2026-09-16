#pragma once
#include "../board.hpp"
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <map>
#include <utility>
#include <vector>
#include <string>
#include <optional>

struct MoveHistoryEntry {
    int piece;

    int fromRow;
    int fromCol;

    int toRow;
    int toCol;

    int capturedPiece;

    Board boardAfterMove;
};

class ChessUI {

public:
    explicit ChessUI(int engineSide = 0, int searchDepth = 2);
    void run();

private:
    Board game;
    sf::RenderWindow window;

    enum class Screen {StartScreen, MainMenu, ChooseSide, Game, Settings, EndGame};

    enum class GameMode {
        PlayerVsPlayer,
        PlayerVsEngine,
        EngineVsEngine
    };

    enum class HistoryStyle {
        Pictogram,
        Algebraic
    };

    struct HistoryNode {
        MoveHistoryEntry move;
        int parent = -1;
        std::vector<int> children;
    };

    Screen currentScreen = Screen::StartScreen;
    Screen previousScreen = Screen::MainMenu;
    GameMode gameMode = GameMode::PlayerVsPlayer;
    HistoryStyle historyStyle = HistoryStyle::Pictogram;
    int engineFrameDelay = 0;

    bool dragPieces = true;
    bool showPossibleMoves = true;
    bool pieceSelected = false;
    int selectedRow = -1;
    int selectedCol = -1;

    std::vector<HistoryNode> historyTree;
    int currentHistoryNode = -1;
    int hoveredHistoryNode = -1;
    float historyHorizontalScroll = 0.f;

    int pieceTheme = 0;
    int settingsTab = 0;
    bool gameStarted = false;

    bool manualBoardFlip = false;
    bool showCoordinates = true;
    bool soundEffects = true;
    bool historyPaused = false;

    std::vector<std::string> pieceThemeFolders;
    std::vector<sf::Texture> pieceThemeKings;

    int boardTheme = 0;
    std::vector<std::string> boardThemeFolders;
    std::vector<sf::Texture> boardWhitePreviews;
    std::vector<sf::Texture> boardBlackPreviews;
    sf::Texture whiteTileTexture;
    sf::Texture blackTileTexture;

    sf::Texture startBackgroundTexture;
    sf::Texture selectionFrameTexture;
    sf::Clock selectionFrameClock;
    int selectionFrame = 1;
    const int selectionFrameCount = 96;

    sf::Font font;
    std::map<int, sf::Texture> pieceTextures;
    std::vector<std::pair<int, int>> legalMoves;
    std::vector<MoveHistoryEntry> moveHistory;
    float historyScroll = 0.f;
    bool dragging = false;
    int draggedRow = -1;
    int draggedCol = -1;
    int engineSearchDepth;
    bool engineStalled = false;
    Board startingBoard;
    int currentHistoryIndex = -1;

    sf::Vector2f mousePosition;

    sf::SoundBuffer moveSoundBuffer;
    sf::SoundBuffer captureSoundBuffer;
    sf::SoundBuffer uiClickSoundBuffer;
    sf::SoundBuffer victorySoundBuffer;
    std::optional<sf::Sound> activeSound;

    const int windowWidth = 1200;
    const int windowHeight = 800;

    const int boardSize = 640;
    const int squareSize = boardSize / 8;

    const int boardX = 50;
    const int boardY = 80;

    const int sidePanelX = 740;
    const int sidePanelY = 80;
    const int sidePanelWidth = 400;
    const int sidePanelHeight = 640;

    void drawHistoryPreview();
    void drawMoveHistory();
    void drawLegalMoves();
    void loadPieceTextures();
    void handleEvents();
    void updateEngine();
    bool applyMove(int fromRow, int fromCol, int toRow, int toCol);
    void draw();
    void drawPieces();
    void drawBoard();
    void drawSidePanel();
    void drawText();
    void drawStartScreen();
    void drawMainMenu();
    void updateSelectionVideo();
    void drawChooseSide();
    void drawSettings();
    void drawGameplaySettings();
    void drawDesignSettings();
    void drawEndScreen();
    void drawCapturedPieces();
    void drawCoordinates();
    void refreshPieceThemes();
    void refreshBoardThemes();
    void loadBoardTextures();
    void drawCoverTexture(const sf::Texture& texture);
    bool isBoardFlipped() const;
    int displayRow(int row) const;
    int displayCol(int col) const;
    void drawHistoryTree();
    std::vector<sf::Vector2f> getHistoryNodePositions() const;
    int getHistoryDepth(int nodeIndex) const;
    void drawMoveArrow(const MoveHistoryEntry& move);
    void drawBoardPosition(Board& board);
    std::string moveToText(const MoveHistoryEntry& move) const;
    void rebuildLinearHistoryFromNode(int nodeIndex);
    void selectPiece(int row, int col);
    void clearSelection();
    void startGame(int engineSide, GameMode mode);
    void playSound(const sf::SoundBuffer& buffer);
};

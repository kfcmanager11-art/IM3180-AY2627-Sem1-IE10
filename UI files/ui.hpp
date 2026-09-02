#pragma once
#include "../board.hpp"
#include <SFML/Graphics.hpp>
#include <map>
#include <vector>


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
    ChessUI();
    void run();

private:
    Board game;
    sf::RenderWindow window;
    sf::Font font;
    std::map<int, sf::Texture> pieceTextures;
    std::vector<std::pair<int, int>> legalMoves;
    std::vector<MoveHistoryEntry> moveHistory;
    float historyScroll = 0.f;
    bool dragging = false;
    int draggedRow = -1;
    int draggedCol = -1;
    Board startingBoard;
    int currentHistoryIndex = -1;

    sf::Vector2f mousePosition;

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
    void draw();
    void drawPieces();
    void drawBoard();
    void drawSidePanel();
    void drawText();
};
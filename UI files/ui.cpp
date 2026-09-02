#include "ui.hpp"
#include <algorithm>

ChessUI::ChessUI()
    : window(sf::VideoMode({1200, 800}), "Chess AI") {

    font.openFromFile("assets/fonts/font.ttf");

    loadPieceTextures();
}

void ChessUI::run() {
    while (window.isOpen()) {
        handleEvents();
        draw();
    }
}

void ChessUI::handleEvents() {

    while (const std::optional event = window.pollEvent()) {
        if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>()) 
        {
            float mouseX = static_cast<float>(wheel->position.x);
            float mouseY = static_cast<float>(wheel->position.y);

            if (mouseX >= sidePanelX && mouseX <= sidePanelX + sidePanelWidth && mouseY >= sidePanelY && mouseY <= sidePanelY + sidePanelHeight) 
            {
                historyScroll -= wheel->delta * 40.f;
                if (historyScroll < 0.f)
                    historyScroll = 0.f;
            }
        }

        if (event->is<sf::Event::Closed>()) { window.close(); }

        if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>()) {

            if (mousePressed->button == sf::Mouse::Button::Left) {

                float mouseX = static_cast<float>(mousePressed->position.x);
                float mouseY = static_cast<float>(mousePressed->position.y);

                const float iconSize = 45.f;
                const float startX = sidePanelX + 25.f;
                const float startY = sidePanelY + 75.f;
                const float spacing = 55.f;

                bool clickedHistory = false;

                for (int i = 0; i < static_cast<int>(moveHistory.size()); i++) 
                {
                    float iconY = startY + i * spacing - historyScroll;

                    if (mouseX >= startX && mouseX <= startX + iconSize && mouseY >= iconY && mouseY <= iconY + iconSize) 
                    {
                        game = moveHistory[i].boardAfterMove;
                        currentHistoryIndex = i;
                        clickedHistory = true;
                        break;
                    }
                }

                int col = static_cast<int>((mouseX - boardX) / squareSize);
                int row = static_cast<int>((mouseY - boardY) / squareSize);

                if (row >= 0 && row < 8 && col >= 0 && col < 8) {
                    auto boardRow = game.begin() + row;

                    if ((*boardRow)[col] != 0) {
                        dragging = true;
                        draggedRow = row;
                        draggedCol = col;
                        mousePosition = { mouseX, mouseY};
                        legalMoves.clear();

                        for (int newRow = 0; newRow < 8; newRow++) {
                            for (int newCol = 0; newCol < 8; newCol++) {
                                if (game.valid_move( draggedRow, draggedCol, newRow, newCol)) {
                                    legalMoves.push_back({newRow, newCol});
                                    }
                             }
                        }
                    }
                }
            }
        }

        if (const auto* mouseMoved = event->getIf<sf::Event::MouseMoved>()) {
            mousePosition = { static_cast<float>(mouseMoved->position.x), static_cast<float>(mouseMoved->position.y)};
        }

        if (const auto* mouseReleased = event->getIf<sf::Event::MouseButtonReleased>()) 
        if (mouseReleased->button == sf::Mouse::Button::Left && dragging) {

            float mouseX = static_cast<float>(mouseReleased->position.x);
            float mouseY = static_cast<float>(mouseReleased->position.y);
            int newCol = static_cast<int>((mouseX - boardX) / squareSize);
            int newRow = static_cast<int>((mouseY - boardY) / squareSize);

            if (newRow >= 0 && newRow < 8 && newCol >= 0 && newCol < 8) {

                if (game.valid_move( draggedRow, draggedCol, newRow, newCol)) {
                    {
                    auto oldRow = game.begin() + draggedRow;
                    auto targetRow = game.begin() + newRow;
                    int movedPiece = (*oldRow)[draggedCol];
                    int capturedPiece = (*targetRow)[newCol];

                    game.make_move( draggedRow, draggedCol, newRow, newCol );

                    moveHistory.push_back({ movedPiece, draggedRow, draggedCol, newRow, newCol, capturedPiece, game });
                    currentHistoryIndex = static_cast<int>(moveHistory.size()) - 1;
                    }
                }
            }

            dragging = false;
            draggedRow = -1;
            draggedCol = -1;
            legalMoves.clear();
        }
    }
    
}

void ChessUI::draw() {

    window.clear(sf::Color(30, 30, 30));

    drawBoard();
    drawLegalMoves();
    drawPieces();
    drawHistoryPreview();
    drawSidePanel();
    drawText();
    drawMoveHistory();

    window.display();
}

void ChessUI::drawBoard() {

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {

            sf::RectangleShape square( sf::Vector2f(squareSize, squareSize) );
            square.setPosition( sf::Vector2f( boardX + col * squareSize, boardY + row * squareSize ) );

            if ((row + col) % 2 == 0) {
                square.setFillColor( sf::Color(235, 220, 190) );
            } else {
                square.setFillColor( sf::Color(120, 80, 60) ); }

            window.draw(square);
        }
    }
}

void ChessUI::drawSidePanel() {

    sf::RectangleShape panel( sf::Vector2f( sidePanelWidth, sidePanelHeight ) );
    panel.setPosition( sf::Vector2f( idePanelX, sidePanelY ) );
    panel.setFillColor( sf::Color(45, 45, 45) );

    window.draw(panel);
}


void ChessUI::drawText() {

    sf::Text historyTitle(font, "MOVE HISTORY", 28);
    historyTitle.setPosition({sidePanelX + 20.f, sidePanelY + 20.f});
    historyTitle.setFillColor(sf::Color::White);
    window.draw(historyTitle);
}
    
void ChessUI::drawPieces() {

    int draggedPiece = 0;
    int row = 0;

    for (auto& boardRow : game) {
        int col = 0;
        for (auto& piece : boardRow) {
            if (piece != 0) {
                if (dragging && row == draggedRow && col == draggedCol) {
                    draggedPiece = piece;
                    col++;
                    continue;
                }
                sf::Sprite sprite(pieceTextures[piece]);

                auto textureSize = pieceTextures[piece].getSize();
                float maxSize = static_cast<float>( std::max(textureSize.x, textureSize.y) );
                float scale = 64.f / maxSize;
                sprite.setScale({scale, scale});

                float width = textureSize.x * scale;
                float height = textureSize.y * scale;
                float x = boardX + col * squareSize + (squareSize - width) / 2.f;
                float y = boardY + row * squareSize + (squareSize - height) / 2.f;
                sprite.setPosition({x, y});
                window.draw(sprite);
            }
            col++;
        }
        row++;
    }


    
    if (dragging && draggedPiece != 0) {

        sf::Sprite sprite(pieceTextures[draggedPiece]);

        auto textureSize =
            pieceTextures[draggedPiece].getSize();

        float maxSize = static_cast<float>(
            std::max(textureSize.x, textureSize.y)
        );

        
        float scale = 120.f / maxSize;

        sprite.setScale({scale, scale});

        float width = textureSize.x * scale;
        float height = textureSize.y * scale;

        sprite.setPosition({
            mousePosition.x - width / 2.f,
            mousePosition.y - height / 2.f
        });

        window.draw(sprite);
    }
}
void ChessUI::loadPieceTextures() {
    pieceTextures[1].loadFromFile("assets/pieces/white_pawn.png");
    pieceTextures[2].loadFromFile("assets/pieces/white_knight.png");
    pieceTextures[3].loadFromFile("assets/pieces/white_bishop.png");
    pieceTextures[4].loadFromFile("assets/pieces/white_rook.png");
    pieceTextures[5].loadFromFile("assets/pieces/white_queen.png");
    pieceTextures[6].loadFromFile("assets/pieces/white_king.png");

    pieceTextures[-1].loadFromFile("assets/pieces/black_pawn.png");
    pieceTextures[-2].loadFromFile("assets/pieces/black_knight.png");
    pieceTextures[-3].loadFromFile("assets/pieces/black_bishop.png");
    pieceTextures[-4].loadFromFile("assets/pieces/black_rook.png");
    pieceTextures[-5].loadFromFile("assets/pieces/black_queen.png");
    pieceTextures[-6].loadFromFile("assets/pieces/black_king.png");
}

void ChessUI::drawLegalMoves() {

    for (const auto& move : legalMoves) {

        int row = move.first;
        int col = move.second;
        auto boardRow = game.begin() + row;
        int pieceOnSquare = (*boardRow)[col];

        if (pieceOnSquare == 0) {

            sf::CircleShape dot(10.f);
            dot.setOrigin({10.f, 10.f});
            dot.setPosition({ boardX + col * squareSize + squareSize / 2.f, boardY + row * squareSize + squareSize / 2.f });
            dot.setFillColor( sf::Color(40, 40, 40, 110) );
            window.draw(dot);
        }

        else {

            sf::CircleShape ring(32.f);
            ring.setOrigin({32.f, 32.f});
            ring.setPosition({ boardX + col * squareSize + squareSize / 2.f, boardY + row * squareSize + squareSize / 2.f });
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineColor( sf::Color(180, 40, 40, 180) );
            ring.setOutlineThickness(5.f);
            window.draw(ring);
        }
    }
}

void ChessUI::drawMoveHistory() {

    const float iconSize = 45.f;
    const float startX = sidePanelX + 25.f;
    const float startY = sidePanelY + 75.f;
    const float spacing = 55.f;

    for (int i = 0; i < moveHistory.size(); i++) {

        int piece = moveHistory[i].piece;

        sf::Sprite sprite(pieceTextures[piece]);

        auto textureSize = pieceTextures[piece].getSize();

        float maxSize = static_cast<float>( std::max(textureSize.x, textureSize.y) );

        float scale = iconSize / maxSize;

        sprite.setScale({scale, scale});

        float width = textureSize.x * scale;
        float height = textureSize.y * scale;

        float x = startX + (iconSize - width) / 2.f;
        float y = startY + i * spacing - historyScroll + (iconSize - height) / 2.f;
        float historyBottom = sidePanelY + sidePanelHeight;

        if (y + iconSize < startY || y + iconSize > historyBottom)
            continue;
        sprite.setPosition({x, y});
        window.draw(sprite);
    }
}

void ChessUI::drawHistoryPreview() {

    const float iconSize = 45.f;
    const float startX = sidePanelX + 25.f;
    const float startY = sidePanelY + 75.f;
    const float spacing = 55.f;

    int hoveredMove = -1;

    for (int i = 0; i < static_cast<int>(moveHistory.size()); i++) {
        float x = startX;
        float y = startY + i * spacing - historyScroll;

        if (mousePosition.x >= x && mousePosition.x <= x + iconSize && mousePosition.y >= y && mousePosition.y <= y + iconSize) {
            hoveredMove = i;
            break;
        }
    }

    if (hoveredMove == -1)
        return;

    const MoveHistoryEntry& move = moveHistory[hoveredMove];
    const float ghostSize = 64.f;


    {
        sf::Sprite sprite(pieceTextures[move.piece]);

        auto textureSize = pieceTextures[move.piece].getSize();
        float maxSize = static_cast<float>( std::max(textureSize.x, textureSize.y) );
        float scale = ghostSize / maxSize;

        sprite.setScale({scale, scale});

        float width = textureSize.x * scale;
        float height = textureSize.y * scale;

        float x = boardX + move.fromCol * squareSize + (squareSize - width) / 2.f;

        float y = boardY + move.fromRow * squareSize + (squareSize - height) / 2.f;

        sprite.setPosition({x, y});

        sprite.setColor( sf::Color(255, 255, 255, 90));
        window.draw(sprite);
    }

    // FADED CAPTURED PIECE ON DESTINATION
    if (move.capturedPiece != 0) {

        sf::Sprite sprite(
            pieceTextures[move.capturedPiece]
        );

        auto textureSize =
            pieceTextures[move.capturedPiece].getSize();

        float maxSize = static_cast<float>(
            std::max(textureSize.x, textureSize.y)
        );

        float scale = ghostSize / maxSize;

        sprite.setScale({scale, scale});

        float width = textureSize.x * scale;
        float height = textureSize.y * scale;

        float x =
            boardX +
            move.toCol * squareSize +
            (squareSize - width) / 2.f;

        float y =
            boardY +
            move.toRow * squareSize +
            (squareSize - height) / 2.f;

        sprite.setPosition({x, y});

        sprite.setColor(
            sf::Color(255, 255, 255, 90)
        );

        window.draw(sprite);
    }
}
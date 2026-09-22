#include "ui.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

ChessUI::ChessUI(int engineSide, int searchDepth,
                 std::unique_ptr<Evaluator> whiteEvaluator,
                 std::unique_ptr<Evaluator> blackEvaluator)
    : game(),
      whiteEngine(0, searchDepth, std::move(whiteEvaluator)),
      blackEngine(1, searchDepth, std::move(blackEvaluator)),
      window(sf::VideoMode({1200, 800}), "Chess AI"),
      engineSearchDepth(searchDepth), configuredEngineSide(engineSide),
      startingBoard(game) {
    if(searchDepth < 1) throw std::invalid_argument("searchDepth must be positive");
    window.setFramerateLimit(60);

    font.openFromFile("assets/fonts/font.ttf");
    startBackgroundTexture.loadFromFile("assets/design/start_background.png");
    selectionFrameTexture.loadFromFile("assets/design/selection_frames/frame001.jpg");

    refreshPieceThemes();
    loadPieceTextures();
    refreshBoardThemes();
    loadBoardTextures();

    moveSoundBuffer.loadFromFile("assets/sounds/move.wav");
    captureSoundBuffer.loadFromFile("assets/sounds/capture.wav");
    uiClickSoundBuffer.loadFromFile("assets/sounds/ui_click.wav");
    victorySoundBuffer.loadFromFile("assets/sounds/victory.wav");
}

void ChessUI::run() {
    while (window.isOpen()) {
        handleEvents();
        if (!window.isOpen()) break;
        draw();
        updateEngine();
    }
}

bool ChessUI::applyMove(int fromRow, int fromCol, int toRow, int toCol) {
    if (!game.valid_move(fromRow, fromCol, toRow, toCol)) return false;

    int movedPiece = (*(game.begin() + fromRow))[fromCol];
    int capturedPiece = (*(game.begin() + toRow))[toCol];

    if (!game.make_move(fromRow, fromCol, toRow, toCol)) return false;

    MoveHistoryEntry entry{movedPiece, fromRow, fromCol, toRow, toCol, capturedPiece, game};

    int newNode = static_cast<int>(historyTree.size());
    historyTree.push_back({entry, currentHistoryNode, {}});

    if (currentHistoryNode >= 0) {
        historyTree[currentHistoryNode].children.push_back(newNode);
    }

    currentHistoryNode = newNode;

    rebuildLinearHistoryFromNode(currentHistoryNode);
    {
        const float spacing = 70.f;
        const float visibleHeight = sidePanelHeight - 90.f;

        int maxDepth = -1;

        for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
            maxDepth = std::max(maxDepth, getHistoryDepth(i));
        }

        const float contentHeight = (maxDepth + 1) * spacing;
        historyScroll = std::max(0.f, contentHeight - visibleHeight);
    }

    engineStalled = false;
    legalMoves.clear();
    clearSelection();

    playSound(capturedPiece != 0 ? captureSoundBuffer : moveSoundBuffer);

    if (game.has_game_ended()) {
        playSound(victorySoundBuffer);
        currentScreen = Screen::EndGame;
    }

    return true;
}

void ChessUI::updateEngine() {
    if (currentScreen != Screen::Game ||
        game.has_game_ended() ||
        engineStalled ||
        historyPaused ||
        gameMode == GameMode::PlayerVsPlayer)
        return;

    if (gameMode == GameMode::PlayerVsEngine) {
        if (game.get_current_turn() != configuredEngineSide)
            return;
    }

    if (gameMode == GameMode::EngineVsEngine) {
        engineFrameDelay++;
        if (engineFrameDelay < 30)
            return;

        engineFrameDelay = 0;
    }

    if (currentHistoryIndex + 1 <
        static_cast<int>(moveHistory.size())) {
        return;
    }

    Engine& engine = engineForTurn();
    if (!engine.find_best_move(game, engineSearchDepth)) {
        engineStalled = true;
        return;
    }

    auto [fromRow, fromCol, toRow, toCol] = engine.get_best_move();

    if (!applyMove(
            fromRow,
            fromCol,
            toRow,
            toCol)) {
        engineStalled = true;
    }
}

void ChessUI::handleEvents() {
    while (const std::optional event = window.pollEvent()) {
        if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>())
        {
            float mouseX = static_cast<float>(wheel->position.x);
            float mouseY = static_cast<float>(wheel->position.y);

            if (currentScreen == Screen::Game &&
                mouseX >= sidePanelX && mouseX <= sidePanelX + sidePanelWidth &&
                mouseY >= sidePanelY && mouseY <= sidePanelY + sidePanelHeight)
            {
                const float iconSize = 45.f;
                const float verticalSpacing = 70.f;
                const float visibleHeight = sidePanelHeight - 90.f;

                int maxDepth = -1;

                for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
                    maxDepth = std::max(maxDepth, getHistoryDepth(i));
                }

                const float contentHeight = (maxDepth + 1) * verticalSpacing;
                const float maxVerticalScroll =
                    std::max(0.f, contentHeight - visibleHeight);

                std::vector<sf::Vector2f> positions = getHistoryNodePositions();
                float contentRight = sidePanelX + 25.f;

                for (const auto& pos : positions) {
                    contentRight = std::max(
                        contentRight,
                        pos.x + historyHorizontalScroll + iconSize
                    );
                }

                const float visibleLeft = sidePanelX + 20.f;
                const float visibleRight = sidePanelX + sidePanelWidth - 20.f;
                const float visibleWidth = visibleRight - visibleLeft;
                const float contentWidth =
                    std::max(visibleWidth, contentRight - visibleLeft);

                const float maxHorizontalScroll =
                    std::max(0.f, contentWidth - visibleWidth);

                bool shiftHeld =
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) ||
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift);

                bool overHorizontalBar =
                    mouseY >= sidePanelY + sidePanelHeight - 28.f;

                if (shiftHeld || overHorizontalBar) {
                    historyHorizontalScroll -= wheel->delta * 40.f;

                    if (historyHorizontalScroll < 0.f)
                        historyHorizontalScroll = 0.f;

                    if (historyHorizontalScroll > maxHorizontalScroll)
                        historyHorizontalScroll = maxHorizontalScroll;
                }

                else {
                    historyScroll -= wheel->delta * 40.f;

                    if (historyScroll < 0.f)
                        historyScroll = 0.f;

                    if (historyScroll > maxVerticalScroll)
                        historyScroll = maxVerticalScroll;
                }
            }
        }

        if (event->is<sf::Event::Closed>()) { window.close(); }

        if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mousePressed->button == sf::Mouse::Button::Left) {
                float mouseX = static_cast<float>(mousePressed->position.x);
                float mouseY = static_cast<float>(mousePressed->position.y);

                if (currentScreen == Screen::StartScreen) {
                    if (mouseX >= 440.f && mouseX <= 760.f &&
                        mouseY >= 250.f && mouseY <= 325.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = Screen::MainMenu;
                        selectionFrame = 1;
                        selectionFrameClock.restart();
                    }

                    continue;
                }

                if (currentScreen == Screen::MainMenu) {
                    if (mouseX >= 440.f && mouseX <= 760.f &&
                        mouseY >= 280.f && mouseY <= 350.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(-1, GameMode::PlayerVsPlayer);
                    }

                    else if (mouseX >= 440.f && mouseX <= 760.f &&
                             mouseY >= 380.f && mouseY <= 450.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = Screen::ChooseSide;
                    }

                    else if (mouseX >= 440.f && mouseX <= 760.f &&
                             mouseY >= 480.f && mouseY <= 550.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(-1, GameMode::EngineVsEngine);
                    }

                    else if (mouseX >= 440.f && mouseX <= 760.f &&
                             mouseY >= 590.f && mouseY <= 650.f) {
                        playSound(uiClickSoundBuffer);
                        previousScreen = Screen::MainMenu;
                        settingsTab = 0;
                        currentScreen = Screen::Settings;
                    }

                    continue;
                }

                if (currentScreen == Screen::ChooseSide) {
                    if (mouseX >= 440.f && mouseX <= 760.f &&
                        mouseY >= 300.f && mouseY <= 370.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(1, GameMode::PlayerVsEngine);
                    }

                    else if (mouseX >= 440.f && mouseX <= 760.f &&
                             mouseY >= 400.f && mouseY <= 470.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(0, GameMode::PlayerVsEngine);
                    }

                    continue;
                }

                if (currentScreen == Screen::Settings) {
                    if (mouseX >= 300.f && mouseX <= 570.f &&
                        mouseY >= 150.f && mouseY <= 205.f) {
                        playSound(uiClickSoundBuffer);
                        settingsTab = 0;
                    }

                    else if (mouseX >= 590.f && mouseX <= 860.f &&
                             mouseY >= 150.f && mouseY <= 205.f) {
                        playSound(uiClickSoundBuffer);
                        settingsTab = 1;
                        refreshPieceThemes();
                        refreshBoardThemes();
                    }

                    else if (settingsTab == 0) {
                        if (mouseX >= 700.f && mouseX <= 820.f &&
                            mouseY >= 235.f && mouseY <= 285.f) {
                            playSound(uiClickSoundBuffer);
                            dragPieces = !dragPieces;
                            clearSelection();
                        }

                        else if (mouseX >= 700.f && mouseX <= 820.f &&
                                 mouseY >= 310.f && mouseY <= 360.f) {
                            playSound(uiClickSoundBuffer);
                            showPossibleMoves = !showPossibleMoves;

                            if (!showPossibleMoves)
                                legalMoves.clear();
                            else if (pieceSelected)
                                selectPiece(selectedRow, selectedCol);
                        }

                        else if (mouseX >= 700.f && mouseX <= 820.f &&
                                 mouseY >= 385.f && mouseY <= 435.f) {
                            playSound(uiClickSoundBuffer);
                            showCoordinates = !showCoordinates;
                        }

                        else if (mouseX >= 700.f && mouseX <= 820.f &&
                                 mouseY >= 460.f && mouseY <= 510.f) {
                            playSound(uiClickSoundBuffer);
                            soundEffects = !soundEffects;
                        }

                        else if (mouseX >= 700.f && mouseX <= 820.f &&
                                 mouseY >= 535.f && mouseY <= 585.f) {
                            playSound(uiClickSoundBuffer);
                            historyStyle = historyStyle == HistoryStyle::Pictogram
                                ? HistoryStyle::Algebraic
                                : HistoryStyle::Pictogram;
                        }
                    }

                    else {
                        const float pieceCardWidth = 90.f;
                        const float pieceCardHeight = 82.f;
                        const float pieceGapX = 18.f;
                        const float pieceStartX = 285.f;
                        const float pieceStartY = 245.f;
                        const int pieceColumns = 6;

                        for (int i = 0; i < static_cast<int>(pieceThemeFolders.size()); i++) {
                            int row = i / pieceColumns;
                            int col = i % pieceColumns;

                            float x = pieceStartX + col * (pieceCardWidth + pieceGapX);
                            float y = pieceStartY + row * 92.f;

                            if (y + pieceCardHeight > 400.f)
                                break;

                            if (mouseX >= x && mouseX <= x + pieceCardWidth &&
                                mouseY >= y && mouseY <= y + pieceCardHeight) {
                                playSound(uiClickSoundBuffer);
                                pieceTheme = i;
                                loadPieceTextures();
                                break;
                            }
                        }

                        const float boardCardWidth = 90.f;
                        const float boardCardHeight = 82.f;
                        const float boardGapX = 18.f;
                        const float boardStartX = 285.f;
                        const float boardStartY = 470.f;
                        const int boardColumns = 6;

                        for (int i = 0; i < static_cast<int>(boardThemeFolders.size()); i++) {
                            int row = i / boardColumns;
                            int col = i % boardColumns;

                            float x = boardStartX + col * (boardCardWidth + boardGapX);
                            float y = boardStartY + row * 92.f;

                            if (y + boardCardHeight > 625.f)
                                break;

                            if (mouseX >= x && mouseX <= x + boardCardWidth &&
                                mouseY >= y && mouseY <= y + boardCardHeight) {
                                playSound(uiClickSoundBuffer);
                                boardTheme = i;
                                loadBoardTextures();
                                break;
                            }
                        }
                    }

                    if (gameStarted &&
                        mouseX >= 300.f && mouseX <= 490.f &&
                        mouseY >= 650.f && mouseY <= 710.f) {
                        playSound(uiClickSoundBuffer);
                        int engineSide = configuredEngineSide;
                        GameMode mode = gameMode;
                        startGame(engineSide, mode);
                        continue;
                    }

                    if (mouseX >= 505.f && mouseX <= 695.f &&
                        mouseY >= 650.f && mouseY <= 710.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = Screen::MainMenu;
                        hoveredHistoryNode = -1;
                        continue;
                    }

                    if (mouseX >= 710.f && mouseX <= 900.f &&
                        mouseY >= 650.f && mouseY <= 710.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = previousScreen;
                        continue;
                    }

                    continue;
                }

                if (currentScreen == Screen::EndGame) {
                    if (mouseX >= 390.f && mouseX <= 590.f &&
                        mouseY >= 500.f && mouseY <= 565.f) {
                        playSound(uiClickSoundBuffer);
                        int engineSide = configuredEngineSide;
                        GameMode mode = gameMode;
                        startGame(engineSide, mode);
                        continue;
                    }

                    if (mouseX >= 610.f && mouseX <= 810.f &&
                        mouseY >= 500.f && mouseY <= 565.f) {
                        playSound(uiClickSoundBuffer);
                        previousScreen = Screen::EndGame;
                        currentScreen = Screen::Settings;
                        settingsTab = 0;
                        continue;
                    }

                    if (mouseX >= 500.f && mouseX <= 700.f &&
                        mouseY >= 590.f && mouseY <= 650.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = Screen::MainMenu;
                        continue;
                    }

                    continue;
                }

                if (currentScreen != Screen::Game)
                    continue;

                if (mouseX >= 800.f && mouseX <= 875.f &&
                    mouseY >= 20.f && mouseY <= 60.f) {
                    playSound(uiClickSoundBuffer);
                    manualBoardFlip = !manualBoardFlip;
                    clearSelection();
                    continue;
                }

                if (historyPaused &&
                    mouseX >= 885.f && mouseX <= 955.f &&
                    mouseY >= 20.f && mouseY <= 60.f) {
                    playSound(uiClickSoundBuffer);
                    historyPaused = false;
                    engineStalled = false;
                    engineFrameDelay = 0;
                    clearSelection();
                    continue;
                }

                if (mouseX >= 970.f && mouseX <= 1130.f &&
                    mouseY >= 20.f && mouseY <= 60.f) {
                    playSound(uiClickSoundBuffer);
                    previousScreen = Screen::Game;
                    settingsTab = 0;
                    currentScreen = Screen::Settings;
                    clearSelection();
                    continue;
                }

                const float iconSize = 45.f;
                std::vector<sf::Vector2f> historyPositions = getHistoryNodePositions();

                bool clickedHistory = false;

                for (int i = 0; i < static_cast<int>(historyTree.size()); i++)
                {
                    float nodeX = historyPositions[i].x;
                    float nodeY = historyPositions[i].y - historyScroll;

                    if (mouseX >= nodeX && mouseX <= nodeX + iconSize &&
                        mouseY >= nodeY && mouseY <= nodeY + iconSize)
                    {
                        playSound(uiClickSoundBuffer);
                        currentHistoryNode = i;
                        game = historyTree[i].move.boardAfterMove;
                        rebuildLinearHistoryFromNode(currentHistoryNode);

                        engineStalled = false;
                        historyPaused = true;
                        clearSelection();

                        clickedHistory = true;
                        break;
                    }
                }

                bool engineTurn = gameMode == GameMode::EngineVsEngine ||
                    (gameMode == GameMode::PlayerVsEngine &&
                     game.get_current_turn() == configuredEngineSide);
                if (clickedHistory || historyPaused || engineTurn || game.has_game_ended()) continue;

                if (mouseX < boardX || mouseX >= boardX + boardSize ||
                    mouseY < boardY || mouseY >= boardY + boardSize) continue;

                int shownCol = static_cast<int>((mouseX - boardX) / squareSize);
                int shownRow = static_cast<int>((mouseY - boardY) / squareSize);

                int col = isBoardFlipped() ? 7 - shownCol : shownCol;
                int row = isBoardFlipped() ? 7 - shownRow : shownRow;

                if (row < 0 || row >= 8 || col < 0 || col >= 8)
                    continue;

                if (!dragPieces) {
                    if (!pieceSelected) {
                        auto boardRow = game.begin() + row;
                        int piece = (*boardRow)[col];

                        if (piece != 0 && (piece < 0) == (game.get_current_turn() == 1)) {
                            selectPiece(row, col);
                        }
                    }

                    else {
                        if (row == selectedRow && col == selectedCol) {
                            clearSelection();
                            continue;
                        }

                        auto boardRow = game.begin() + row;
                        int piece = (*boardRow)[col];

                        if (piece != 0 && (piece < 0) == (game.get_current_turn() == 1)) {
                            selectPiece(row, col);
                            continue;
                        }

                        if (applyMove(selectedRow, selectedCol, row, col)) {
                            clearSelection();
                        }
                    }

                    continue;
                }

                auto boardRow = game.begin() + row;
                int piece = (*boardRow)[col];

                if (piece != 0 && (piece < 0) == (game.get_current_turn() == 1)) {
                    dragging = true;
                    draggedRow = row;
                    draggedCol = col;
                    mousePosition = {mouseX, mouseY};
                    legalMoves.clear();

                    if (showPossibleMoves) {
                        for (int newRow = 0; newRow < 8; newRow++) {
                            for (int newCol = 0; newCol < 8; newCol++) {
                                if (game.valid_move(draggedRow, draggedCol, newRow, newCol)) {
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
            int shownCol = static_cast<int>((mouseX - boardX) / squareSize);
            int shownRow = static_cast<int>((mouseY - boardY) / squareSize);

            int newCol = isBoardFlipped() ? 7 - shownCol : shownCol;
            int newRow = isBoardFlipped() ? 7 - shownRow : shownRow;

            bool engineTurn = gameMode == GameMode::EngineVsEngine ||
                (gameMode == GameMode::PlayerVsEngine &&
                 game.get_current_turn() == configuredEngineSide);
            if (!engineTurn && !game.has_game_ended() &&
                mouseX >= boardX && mouseX < boardX + boardSize &&
                mouseY >= boardY && mouseY < boardY + boardSize) {
                applyMove(draggedRow, draggedCol, newRow, newCol);
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

    if (currentScreen == Screen::StartScreen) {
        drawStartScreen();
        window.display();
        return;
    }

    if (currentScreen == Screen::MainMenu) {
        drawMainMenu();
        window.display();
        return;
    }

    if (currentScreen == Screen::ChooseSide) {
        drawChooseSide();
        window.display();
        return;
    }

    if (currentScreen == Screen::Settings) {
        drawSettings();
        window.display();
        return;
    }

    if (currentScreen == Screen::EndGame) {
        drawBoard();

        if (showCoordinates)
            drawCoordinates();

        drawPieces();
        drawCapturedPieces();
        drawSidePanel();
        drawText();
        drawMoveHistory();
        drawEndScreen();
        window.display();
        return;
    }

    drawBoard();

    if (showCoordinates)
        drawCoordinates();

    drawLegalMoves();
    drawPieces();

    if (!dragPieces && pieceSelected) {
        sf::RectangleShape selectedSquare( sf::Vector2f(squareSize - 6.f, squareSize - 6.f) );
        selectedSquare.setPosition({
            static_cast<float>(boardX + displayCol(selectedCol) * squareSize + 3),
            static_cast<float>(boardY + displayRow(selectedRow) * squareSize + 3)
        });
        selectedSquare.setFillColor(sf::Color::Transparent);
        selectedSquare.setOutlineColor(sf::Color(255, 215, 0));
        selectedSquare.setOutlineThickness(4.f);
        window.draw(selectedSquare);
    }

    drawHistoryPreview();
    drawCapturedPieces();

    drawSidePanel();
    drawText();
    drawMoveHistory();

    sf::RectangleShape flipButton({75.f, 40.f});
    flipButton.setPosition({800.f, 20.f});
    flipButton.setFillColor(sf::Color(60, 60, 60));
    window.draw(flipButton);

    sf::Text flipText(font, "FLIP", 15);
    flipText.setPosition({818.f, 30.f});
    flipText.setFillColor(sf::Color::White);
    window.draw(flipText);

    if (historyPaused) {
        sf::RectangleShape playButton({70.f, 40.f});
        playButton.setPosition({885.f, 20.f});
        playButton.setFillColor(sf::Color(70, 120, 75));
        window.draw(playButton);

        sf::Text playText(font, "PLAY", 15);
        playText.setPosition({899.f, 30.f});
        playText.setFillColor(sf::Color::White);
        window.draw(playText);
    }

    sf::RectangleShape settingsButton({160.f, 40.f});
    settingsButton.setPosition({970.f, 20.f});
    settingsButton.setFillColor(sf::Color(60, 60, 60));
    window.draw(settingsButton);

    sf::Text settingsText(font, "SETTINGS", 18);
    settingsText.setPosition({1005.f, 28.f});
    settingsText.setFillColor(sf::Color::White);
    window.draw(settingsText);

    window.display();
}

void ChessUI::drawCoverTexture(const sf::Texture& texture) {
    if (texture.getSize().x == 0 || texture.getSize().y == 0)
        return;

    sf::Sprite background(texture);

    auto textureSize = texture.getSize();
    float scaleX = static_cast<float>(windowWidth) / textureSize.x;
    float scaleY = static_cast<float>(windowHeight) / textureSize.y;
    float scale = std::max(scaleX, scaleY);

    background.setScale({scale, scale});

    float width = textureSize.x * scale;
    float height = textureSize.y * scale;

    background.setPosition({
        (windowWidth - width) / 2.f,
        (windowHeight - height) / 2.f
    });

    window.draw(background);
}

void ChessUI::drawStartScreen() {
    drawCoverTexture(startBackgroundTexture);

    sf::RectangleShape darkOverlay({1200.f, 800.f});
    darkOverlay.setFillColor(sf::Color(0, 0, 0, 55));
    window.draw(darkOverlay);

    sf::Text title(font, "CHESS", 64);
    title.setPosition({500.f, 105.f});
    title.setFillColor(sf::Color::White);
    window.draw(title);

    sf::RectangleShape startButton({320.f, 75.f});
    startButton.setPosition({440.f, 250.f});
    startButton.setFillColor(sf::Color(20, 20, 20, 205));
    startButton.setOutlineColor(sf::Color(220, 220, 220));
    startButton.setOutlineThickness(2.f);
    window.draw(startButton);

    sf::Text startText(font, "START", 28);
    startText.setPosition({550.f, 270.f});
    startText.setFillColor(sf::Color::White);
    window.draw(startText);
}

void ChessUI::updateSelectionVideo() {
    if (selectionFrameClock.getElapsedTime().asSeconds() < 0.125f)
        return;

    selectionFrameClock.restart();

    selectionFrame++;

    if (selectionFrame > selectionFrameCount)
        selectionFrame = 1;

    std::ostringstream filename;

    filename << "assets/design/selection_frames/frame"
             << std::setfill('0') << std::setw(3)
             << selectionFrame << ".jpg";

    selectionFrameTexture.loadFromFile(filename.str());
}

void ChessUI::drawMainMenu() {
    updateSelectionVideo();

    drawCoverTexture(selectionFrameTexture);

    sf::RectangleShape darkOverlay({1200.f, 800.f});
    darkOverlay.setFillColor(sf::Color(0, 0, 0, 95));
    window.draw(darkOverlay);

    sf::Text title(font, "CHOOSE GAME MODE", 42);
    title.setPosition({390.f, 105.f});
    title.setFillColor(sf::Color::White);
    window.draw(title);

    const float buttonWidth = 320.f;
    const float buttonHeight = 70.f;
    const float buttonX = 440.f;

    sf::RectangleShape pvpButton({buttonWidth, buttonHeight});
    pvpButton.setPosition({buttonX, 280.f});
    pvpButton.setFillColor(sf::Color(20, 20, 20, 210));
    pvpButton.setOutlineColor(sf::Color(210, 210, 210));
    pvpButton.setOutlineThickness(1.f);
    window.draw(pvpButton);

    sf::Text pvpText(font, "PLAYER VS PLAYER", 24);
    pvpText.setPosition({475.f, 300.f});
    pvpText.setFillColor(sf::Color::White);
    window.draw(pvpText);

    sf::RectangleShape pveButton({buttonWidth, buttonHeight});
    pveButton.setPosition({buttonX, 380.f});
    pveButton.setFillColor(sf::Color(20, 20, 20, 210));
    pveButton.setOutlineColor(sf::Color(210, 210, 210));
    pveButton.setOutlineThickness(1.f);
    window.draw(pveButton);

    sf::Text pveText(font, "PLAYER VS ENGINE", 24);
    pveText.setPosition({465.f, 400.f});
    pveText.setFillColor(sf::Color::White);
    window.draw(pveText);

    sf::RectangleShape eveButton({buttonWidth, buttonHeight});
    eveButton.setPosition({buttonX, 480.f});
    eveButton.setFillColor(sf::Color(20, 20, 20, 210));
    eveButton.setOutlineColor(sf::Color(210, 210, 210));
    eveButton.setOutlineThickness(1.f);
    window.draw(eveButton);

    sf::Text eveText(font, "ENGINE VS ENGINE", 24);
    eveText.setPosition({465.f, 500.f});
    eveText.setFillColor(sf::Color::White);
    window.draw(eveText);

    sf::RectangleShape settingsButton({buttonWidth, 60.f});
    settingsButton.setPosition({buttonX, 590.f});
    settingsButton.setFillColor(sf::Color(20, 20, 20, 210));
    settingsButton.setOutlineColor(sf::Color(210, 210, 210));
    settingsButton.setOutlineThickness(1.f);
    window.draw(settingsButton);

    sf::Text settingsText(font, "SETTINGS", 22);
    settingsText.setPosition({545.f, 607.f});
    settingsText.setFillColor(sf::Color::White);
    window.draw(settingsText);
}

void ChessUI::drawChooseSide() {
    sf::Text title(font, "CHOOSE YOUR SIDE", 42);
    title.setPosition({390.f, 140.f});
    title.setFillColor(sf::Color::White);
    window.draw(title);

    const float buttonWidth = 320.f;
    const float buttonHeight = 70.f;
    const float buttonX = 440.f;

    sf::RectangleShape whiteButton({buttonWidth, buttonHeight});
    whiteButton.setPosition({buttonX, 300.f});
    whiteButton.setFillColor(sf::Color(60, 60, 60));
    window.draw(whiteButton);

    sf::Text whiteText(font, "PLAY AS WHITE", 24);
    whiteText.setPosition({490.f, 320.f});
    whiteText.setFillColor(sf::Color::White);
    window.draw(whiteText);

    sf::RectangleShape blackButton({buttonWidth, buttonHeight});
    blackButton.setPosition({buttonX, 400.f});
    blackButton.setFillColor(sf::Color(60, 60, 60));
    window.draw(blackButton);

    sf::Text blackText(font, "PLAY AS BLACK", 24);
    blackText.setPosition({490.f, 420.f});
    blackText.setFillColor(sf::Color::White);
    window.draw(blackText);
}

bool ChessUI::isBoardFlipped() const {
    bool automaticFlip =
        gameMode == GameMode::PlayerVsEngine &&
        configuredEngineSide == 1;

    return automaticFlip != manualBoardFlip;
}

int ChessUI::displayRow(int row) const { return isBoardFlipped() ? 7 - row : row; }
int ChessUI::displayCol(int col) const { return isBoardFlipped() ? 7 - col : col; }

void ChessUI::drawBoard() {
    bool useTextures =
        whiteTileTexture.getSize().x > 0 &&
        blackTileTexture.getSize().x > 0;

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            float x = static_cast<float>(boardX + col * squareSize);
            float y = static_cast<float>(boardY + row * squareSize);

            if (useTextures) {
                sf::Texture& tileTexture =
                    ((row + col) % 2 == 0)
                    ? whiteTileTexture
                    : blackTileTexture;

                sf::Sprite tile(tileTexture);

                auto size = tileTexture.getSize();

                tile.setScale({
                    static_cast<float>(squareSize) / size.x,
                    static_cast<float>(squareSize) / size.y
                });

                tile.setPosition({x, y});
                window.draw(tile);
            }

            else {
                sf::RectangleShape square(
                    sf::Vector2f(
                        static_cast<float>(squareSize),
                        static_cast<float>(squareSize)
                    )
                );

                square.setPosition({x, y});

                if ((row + col) % 2 == 0)
                    square.setFillColor(sf::Color(235, 220, 190));
                else
                    square.setFillColor(sf::Color(120, 80, 60));

                window.draw(square);
            }
        }
    }
}

void ChessUI::drawSidePanel() {
    sf::RectangleShape panel( sf::Vector2f( sidePanelWidth, sidePanelHeight ) );
    panel.setPosition( sf::Vector2f( sidePanelX, sidePanelY ) );
    panel.setFillColor( sf::Color(45, 45, 45) );

    window.draw(panel);
}

void ChessUI::drawText() {
    std::string mode;

    if (gameMode == GameMode::PlayerVsPlayer) {
        mode = "Player vs Player";
    }

    else if (gameMode == GameMode::PlayerVsEngine) {
        if (configuredEngineSide == 0)
            mode = "Player vs Engine | Player: Black";
        else
            mode = "Player vs Engine | Player: White";
    }

    else {
        mode = "Engine vs Engine";
    }
    std::string turnText = game.get_current_turn() == 0 ? "White" : "Black";
    std::string status = game.has_game_ended() ? "Game ended" :
        turnText + " to move (" + std::to_string(game.get_moves_left()) + " actions left)";
    if (engineStalled) status = "Engine has no legal move";
    sf::Text turnLabel(font, mode + " | " + status, 20);
    turnLabel.setPosition({static_cast<float>(boardX), 35.f});
    window.draw(turnLabel);

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
                int shownCol = displayCol(col);
                int shownRow = displayRow(row);

                float x = boardX + shownCol * squareSize + (squareSize - width) / 2.f;
                float y = boardY + shownRow * squareSize + (squareSize - height) / 2.f;
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
void ChessUI::refreshPieceThemes() {
    pieceThemeFolders.clear();
    pieceThemeKings.clear();

    namespace fs = std::filesystem;

    if (!fs::exists("assets"))
        return;

    std::vector<std::pair<int, std::string>> found;

    for (const auto& entry : fs::directory_iterator("assets")) {
        if (!entry.is_directory())
            continue;

        std::string name = entry.path().filename().string();

        if (name == "pieces") {
            found.push_back({1, entry.path().string() + "/"});
        }

        else if (name.rfind("pieces", 0) == 0) {
            std::string numberText = name.substr(6);

            if (numberText.empty())
                continue;

            try {
                int number = std::stoi(numberText);
                found.push_back({number, entry.path().string() + "/"});
            }
            catch (...) {
            }
        }
    }

    std::sort(found.begin(), found.end(),
        [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

    for (const auto& theme : found) {
        pieceThemeFolders.push_back(theme.second);

        sf::Texture kingTexture;
        kingTexture.loadFromFile(theme.second + "white_king.png");
        pieceThemeKings.push_back(std::move(kingTexture));
    }

    if (pieceThemeFolders.empty()) {
        pieceThemeFolders.push_back("assets/pieces/");

        sf::Texture kingTexture;
        kingTexture.loadFromFile("assets/pieces/white_king.png");
        pieceThemeKings.push_back(std::move(kingTexture));
    }

    if (pieceTheme >= static_cast<int>(pieceThemeFolders.size()))
        pieceTheme = 0;
}

void ChessUI::refreshBoardThemes() {
    boardThemeFolders.clear();
    boardWhitePreviews.clear();
    boardBlackPreviews.clear();

    namespace fs = std::filesystem;

    if (!fs::exists("assets"))
        return;

    std::vector<std::pair<int, std::string>> found;

    for (const auto& entry : fs::directory_iterator("assets")) {
        if (!entry.is_directory())
            continue;

        std::string name = entry.path().filename().string();

        if (name == "board") {
            found.push_back({0, entry.path().string() + "/"});
        }

        else if (name.rfind("board", 0) == 0) {
            std::string numberText = name.substr(5);

            if (numberText.empty())
                continue;

            try {
                int number = std::stoi(numberText);
                found.push_back({number, entry.path().string() + "/"});
            }
            catch (...) {
            }
        }
    }

    std::sort(found.begin(), found.end(),
        [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

    for (const auto& theme : found) {
        const std::string& folder = theme.second;

        if (!fs::exists(folder + "white_tile.png") ||
            !fs::exists(folder + "black_tile.png"))
            continue;

        boardThemeFolders.push_back(folder);

        sf::Texture whitePreview;
        sf::Texture blackPreview;

        whitePreview.loadFromFile(folder + "white_tile.png");
        blackPreview.loadFromFile(folder + "black_tile.png");

        boardWhitePreviews.push_back(std::move(whitePreview));
        boardBlackPreviews.push_back(std::move(blackPreview));
    }

    if (boardTheme >= static_cast<int>(boardThemeFolders.size()))
        boardTheme = 0;
}

void ChessUI::loadBoardTextures() {
    if (boardThemeFolders.empty()) {
        whiteTileTexture = sf::Texture();
        blackTileTexture = sf::Texture();
        return;
    }

    if (boardTheme < 0 || boardTheme >= static_cast<int>(boardThemeFolders.size()))
        boardTheme = 0;

    std::string folder = boardThemeFolders[boardTheme];

    whiteTileTexture.loadFromFile(folder + "white_tile.png");
    blackTileTexture.loadFromFile(folder + "black_tile.png");
}

void ChessUI::loadPieceTextures() {
    if (pieceThemeFolders.empty())
        refreshPieceThemes();

    if (pieceTheme < 0 || pieceTheme >= static_cast<int>(pieceThemeFolders.size()))
        pieceTheme = 0;

    std::string folder = pieceThemeFolders[pieceTheme];

    pieceTextures[1].loadFromFile(folder + "white_pawn.png");
    pieceTextures[2].loadFromFile(folder + "white_knight.png");
    pieceTextures[3].loadFromFile(folder + "white_bishop.png");
    pieceTextures[4].loadFromFile(folder + "white_rook.png");
    pieceTextures[5].loadFromFile(folder + "white_queen.png");
    pieceTextures[6].loadFromFile(folder + "white_king.png");

    pieceTextures[-1].loadFromFile(folder + "black_pawn.png");
    pieceTextures[-2].loadFromFile(folder + "black_knight.png");
    pieceTextures[-3].loadFromFile(folder + "black_bishop.png");
    pieceTextures[-4].loadFromFile(folder + "black_rook.png");
    pieceTextures[-5].loadFromFile(folder + "black_queen.png");
    pieceTextures[-6].loadFromFile(folder + "black_king.png");
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
            int shownCol = displayCol(col);
            int shownRow = displayRow(row);
            dot.setPosition({ boardX + shownCol * squareSize + squareSize / 2.f, boardY + shownRow * squareSize + squareSize / 2.f});
            dot.setFillColor( sf::Color(40, 40, 40, 110) );
            window.draw(dot);
        }

        else {
            sf::CircleShape ring(32.f);
            ring.setOrigin({32.f, 32.f});
            int shownCol = displayCol(col);
            int shownRow = displayRow(row);
            ring.setPosition({ boardX + shownCol * squareSize + squareSize / 2.f, boardY + shownRow * squareSize + squareSize / 2.f });
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineColor( sf::Color(180, 40, 40, 180) );
            ring.setOutlineThickness(5.f);
            window.draw(ring);
        }
    }
}

void ChessUI::drawMoveHistory() {
    if (historyTree.empty())
        return;

    const float iconSize = 45.f;
    std::vector<sf::Vector2f> positions = getHistoryNodePositions();

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        int parent = historyTree[i].parent;

        if (parent < 0)
            continue;

        float x1 = positions[parent].x + iconSize / 2.f;
        float y1 = positions[parent].y - historyScroll + iconSize / 2.f;

        float x2 = positions[i].x + iconSize / 2.f;
        float y2 = positions[i].y - historyScroll + iconSize / 2.f;

        float historyTop = sidePanelY + 70.f;
        float historyBottom = sidePanelY + sidePanelHeight;

        if (y1 - iconSize / 2.f < historyTop || y1 + iconSize / 2.f > historyBottom || y2 - iconSize / 2.f < historyTop || y2 + iconSize / 2.f > historyBottom)
            continue;

        float dx = x2 - x1;
        float dy = y2 - y1;

        float length = std::sqrt(dx * dx + dy * dy);
        float angle = std::atan2(dy, dx) * 180.f / 3.14159265f;

        sf::RectangleShape line({length, 3.f});
        line.setOrigin({0.f, 1.5f});
        line.setPosition({x1, y1});
        line.setRotation(sf::degrees(angle));
        line.setFillColor(sf::Color(120, 120, 120));

        window.draw(line);
    }

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        float x = positions[i].x;
        float y = positions[i].y - historyScroll;
        float historyTop = sidePanelY + 70.f;
        float historyBottom = sidePanelY + sidePanelHeight;

        if (y < historyTop || y + iconSize > historyBottom)
            continue;

        bool currentNode = i == currentHistoryNode;

        sf::RectangleShape nodeBox({iconSize, iconSize});
        nodeBox.setPosition({x, y});
        nodeBox.setFillColor(sf::Color(55, 55, 55));

        if (currentNode) {
            nodeBox.setOutlineColor(sf::Color(255, 215, 0));
            nodeBox.setOutlineThickness(3.f);
        }

        window.draw(nodeBox);

        const MoveHistoryEntry& move = historyTree[i].move;

        if (historyStyle == HistoryStyle::Pictogram) {
            int piece = move.piece;
            sf::Sprite sprite(pieceTextures[piece]);

            auto textureSize = pieceTextures[piece].getSize();
            float maxSize = static_cast<float>( std::max(textureSize.x, textureSize.y) );
            float scale = 36.f / maxSize;

            sprite.setScale({scale, scale});

            float width = textureSize.x * scale;
            float height = textureSize.y * scale;

            sprite.setPosition({
                x + (iconSize - width) / 2.f,
                y + (iconSize - height) / 2.f
            });

            window.draw(sprite);
        }

        else {
            sf::Text moveText(font, moveToText(move), 11);
            moveText.setPosition({x + 3.f, y + 14.f});
            moveText.setFillColor(sf::Color::White);

            window.draw(moveText);
        }
    }

    const auto& positionsForScroll = positions;

    float contentRight = sidePanelX + 20.f;

    for (const auto& pos : positionsForScroll) {
        contentRight = std::max(
            contentRight,
            pos.x + historyHorizontalScroll + 45.f
        );
    }

    const float visibleLeft = sidePanelX + 20.f;
    const float visibleRight = sidePanelX + sidePanelWidth - 20.f;
    const float visibleWidth = visibleRight - visibleLeft;
    const float contentWidth =
        std::max(visibleWidth, contentRight - visibleLeft);

    if (contentWidth > visibleWidth) {
        const float trackY = sidePanelY + sidePanelHeight - 18.f;

        sf::RectangleShape track({visibleWidth, 7.f});
        track.setPosition({visibleLeft, trackY});
        track.setFillColor(sf::Color(70, 70, 70));
        window.draw(track);

        float thumbWidth =
            visibleWidth * (visibleWidth / contentWidth);

        thumbWidth = std::max(45.f, thumbWidth);

        const float maxHorizontalScroll =
            contentWidth - visibleWidth;

        const float thumbTravel =
            visibleWidth - thumbWidth;

        float thumbX = visibleLeft;

        if (maxHorizontalScroll > 0.f) {
            thumbX +=
                (historyHorizontalScroll / maxHorizontalScroll)
                * thumbTravel;
        }

        sf::RectangleShape thumb({thumbWidth, 9.f});
        thumb.setPosition({thumbX, trackY - 1.f});
        thumb.setFillColor(sf::Color(175, 175, 175));
        window.draw(thumb);
    }
}

void ChessUI::drawHistoryPreview() {
    const float historyLeft = sidePanelX;
    const float historyRight = sidePanelX + sidePanelWidth;
    const float historyTop = sidePanelY;
    const float historyBottom = sidePanelY + sidePanelHeight;

    bool mouseInsideHistory =
        mousePosition.x >= historyLeft &&
        mousePosition.x <= historyRight &&
        mousePosition.y >= historyTop &&
        mousePosition.y <= historyBottom;

    if (!mouseInsideHistory) {
        hoveredHistoryNode = -1;
        return;
    }

    if (historyTree.empty())
        return;

    const float iconSize = 45.f;
    std::vector<sf::Vector2f> positions = getHistoryNodePositions();

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        float x = positions[i].x;
        float y = positions[i].y - historyScroll;

        const float nodeTop = sidePanelY + 70.f;
        const float nodeBottom = sidePanelY + sidePanelHeight - 30.f;

        if (x + iconSize < historyLeft || x > historyRight)
            continue;

        if (y < nodeTop || y + iconSize > nodeBottom)
            continue;

        if (mousePosition.x >= x &&
            mousePosition.x <= x + iconSize &&
            mousePosition.y >= y &&
            mousePosition.y <= y + iconSize) {
            hoveredHistoryNode = i;
            break;
        }
    }

    if (hoveredHistoryNode == -1)
        return;

    int parent = historyTree[hoveredHistoryNode].parent;

    Board previewBoard = parent == -1
        ? startingBoard
        : historyTree[parent].move.boardAfterMove;

    drawBoardPosition(previewBoard);
    drawMoveArrow(historyTree[hoveredHistoryNode].move);
}

int ChessUI::getHistoryDepth(int nodeIndex) const {
    int depth = 0;
    int node = nodeIndex;

    while (node >= 0 && historyTree[node].parent >= 0) {
        depth++;
        node = historyTree[node].parent;
    }

    return depth;
}

std::vector<sf::Vector2f> ChessUI::getHistoryNodePositions() const {
    std::vector<sf::Vector2f> positions(historyTree.size());

    if (historyTree.empty())
        return positions;

    std::vector<int> branchColumn(historyTree.size(), 0);
    int nextFreeColumn = 1;

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        int parent = historyTree[i].parent;

        if (parent < 0) {
            branchColumn[i] = 0;
            continue;
        }

        const std::vector<int>& siblings = historyTree[parent].children;

        int childNumber = 0;

        for (int j = 0; j < static_cast<int>(siblings.size()); j++) {
            if (siblings[j] == i) {
                childNumber = j;
                break;
            }
        }

        if (childNumber == 0) {
            branchColumn[i] = branchColumn[parent];
        }

        else {
            branchColumn[i] = nextFreeColumn;
            nextFreeColumn++;
        }
    }

    const float startX = sidePanelX + 25.f;
    const float startY = sidePanelY + 75.f;
    const float branchSpacing = 62.f;
    const float rowSpacing = 70.f;

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        int depth = getHistoryDepth(i);

        positions[i] = {
            startX + branchColumn[i] * branchSpacing - historyHorizontalScroll,
            startY + depth * rowSpacing
        };
    }

    return positions;
}

void ChessUI::drawSettings() {
    sf::RectangleShape windowBox({720.f, 650.f});
    windowBox.setPosition({240.f, 75.f});
    windowBox.setFillColor(sf::Color(42, 42, 42));
    window.draw(windowBox);

    sf::Text title(font, "SETTINGS", 36);
    title.setPosition({500.f, 95.f});
    title.setFillColor(sf::Color::White);
    window.draw(title);

    sf::RectangleShape gameplayTab({270.f, 55.f});
    gameplayTab.setPosition({300.f, 150.f});
    gameplayTab.setFillColor(settingsTab == 0 ? sf::Color(85, 85, 85) : sf::Color(55, 55, 55));
    window.draw(gameplayTab);

    sf::Text gameplayText(font, "GAMEPLAY", 22);
    gameplayText.setPosition({375.f, 165.f});
    gameplayText.setFillColor(sf::Color::White);
    window.draw(gameplayText);

    sf::RectangleShape designTab({270.f, 55.f});
    designTab.setPosition({590.f, 150.f});
    designTab.setFillColor(settingsTab == 1 ? sf::Color(85, 85, 85) : sf::Color(55, 55, 55));
    window.draw(designTab);

    sf::Text designText(font, "DESIGN", 22);
    designText.setPosition({680.f, 165.f});
    designText.setFillColor(sf::Color::White);
    window.draw(designText);

    if (settingsTab == 0)
        drawGameplaySettings();
    else
        drawDesignSettings();

    if (gameStarted) {
        sf::RectangleShape playAgainButton({190.f, 60.f});
        playAgainButton.setPosition({300.f, 650.f});
        playAgainButton.setFillColor(sf::Color(70, 120, 75));
        window.draw(playAgainButton);

        sf::Text playAgainText(font, "PLAY AGAIN", 18);
        playAgainText.setPosition({340.f, 669.f});
        playAgainText.setFillColor(sf::Color::White);
        window.draw(playAgainText);
    }

    sf::RectangleShape menuButton({190.f, 60.f});
    menuButton.setPosition({505.f, 650.f});
    menuButton.setFillColor(sf::Color(65, 65, 65));
    window.draw(menuButton);

    sf::Text menuText(font, "MAIN MENU", 18);
    menuText.setPosition({548.f, 669.f});
    menuText.setFillColor(sf::Color::White);
    window.draw(menuText);

    sf::RectangleShape backButton({190.f, 60.f});
    backButton.setPosition({710.f, 650.f});
    backButton.setFillColor(sf::Color(55, 55, 55));
    window.draw(backButton);

    sf::Text backText(font, "BACK", 18);
    backText.setPosition({775.f, 669.f});
    backText.setFillColor(sf::Color::White);
    window.draw(backText);
}

void ChessUI::drawGameplaySettings() {
    auto drawToggle = [&](const std::string& label, bool enabled, float y) {
        sf::Text labelText(font, label, 20);
        labelText.setPosition({330.f, y + 9.f});
        labelText.setFillColor(sf::Color::White);
        window.draw(labelText);

        sf::RectangleShape toggle({120.f, 50.f});
        toggle.setPosition({700.f, y});
        toggle.setFillColor(
            enabled
            ? sf::Color(70, 140, 80)
            : sf::Color(100, 100, 100)
        );
        window.draw(toggle);

        sf::Text state(font, enabled ? "ON" : "OFF", 17);
        state.setPosition({742.f, y + 14.f});
        state.setFillColor(sf::Color::White);
        window.draw(state);
    };

    drawToggle("Drag pieces", dragPieces, 235.f);
    drawToggle("Show possible moves", showPossibleMoves, 310.f);
    drawToggle("Board coordinates", showCoordinates, 385.f);
    drawToggle("Sound effects", soundEffects, 460.f);

    sf::Text historyText(font, "Move history", 20);
    historyText.setPosition({330.f, 544.f});
    historyText.setFillColor(sf::Color::White);
    window.draw(historyText);

    sf::RectangleShape historyToggle({120.f, 50.f});
    historyToggle.setPosition({700.f, 535.f});
    historyToggle.setFillColor(sf::Color(70, 70, 70));
    window.draw(historyToggle);

    sf::Text historyState(
        font,
        historyStyle == HistoryStyle::Pictogram ? "ICONS" : "TEXT",
        15
    );
    historyState.setPosition({729.f, 551.f});
    historyState.setFillColor(sf::Color::White);
    window.draw(historyState);
}

void ChessUI::drawDesignSettings() {
    sf::Text pieceTitle(font, "Piece style", 20);
    pieceTitle.setPosition({285.f, 215.f});
    pieceTitle.setFillColor(sf::Color(220, 220, 220));
    window.draw(pieceTitle);

    const float pieceCardWidth = 90.f;
    const float pieceCardHeight = 82.f;
    const float pieceGapX = 18.f;
    const float pieceStartX = 285.f;
    const float pieceStartY = 245.f;
    const int pieceColumns = 6;

    for (int i = 0; i < static_cast<int>(pieceThemeFolders.size()); i++) {
        int row = i / pieceColumns;
        int col = i % pieceColumns;

        float x = pieceStartX + col * (pieceCardWidth + pieceGapX);
        float y = pieceStartY + row * 92.f;

        if (y + pieceCardHeight > 400.f)
            break;

        sf::RectangleShape card({pieceCardWidth, pieceCardHeight});
        card.setPosition({x, y});
        card.setFillColor(
            i == pieceTheme
            ? sf::Color(82, 115, 82)
            : sf::Color(58, 58, 58)
        );

        if (i == pieceTheme) {
            card.setOutlineColor(sf::Color::White);
            card.setOutlineThickness(2.f);
        }

        window.draw(card);

        if (i < static_cast<int>(pieceThemeKings.size()) &&
            pieceThemeKings[i].getSize().x > 0) {
            sf::Sprite king(pieceThemeKings[i]);

            auto size = pieceThemeKings[i].getSize();
            float maxSize = static_cast<float>(std::max(size.x, size.y));
            float scale = 60.f / maxSize;

            king.setScale({scale, scale});

            float width = size.x * scale;
            float height = size.y * scale;

            king.setPosition({
                x + (pieceCardWidth - width) / 2.f,
                y + (pieceCardHeight - height) / 2.f
            });

            window.draw(king);
        }
    }

    sf::Text boardTitle(font, "Board style", 20);
    boardTitle.setPosition({285.f, 440.f});
    boardTitle.setFillColor(sf::Color(220, 220, 220));
    window.draw(boardTitle);

    const float boardCardWidth = 90.f;
    const float boardCardHeight = 82.f;
    const float boardGapX = 18.f;
    const float boardStartX = 285.f;
    const float boardStartY = 470.f;
    const int boardColumns = 6;

    if (boardThemeFolders.empty()) {
        sf::Text noBoards(
            font,
            "Add assets/board with white_tile.png and black_tile.png",
            15
        );

        noBoards.setPosition({285.f, 490.f});
        noBoards.setFillColor(sf::Color(170, 170, 170));
        window.draw(noBoards);
    }

    for (int i = 0; i < static_cast<int>(boardThemeFolders.size()); i++) {
        int row = i / boardColumns;
        int col = i % boardColumns;

        float x = boardStartX + col * (boardCardWidth + boardGapX);
        float y = boardStartY + row * 92.f;

        if (y + boardCardHeight > 625.f)
            break;

        sf::RectangleShape card({boardCardWidth, boardCardHeight});
        card.setPosition({x, y});
        card.setFillColor(
            i == boardTheme
            ? sf::Color(82, 115, 82)
            : sf::Color(58, 58, 58)
        );

        if (i == boardTheme) {
            card.setOutlineColor(sf::Color::White);
            card.setOutlineThickness(2.f);
        }

        window.draw(card);

        if (i < static_cast<int>(boardWhitePreviews.size()) &&
            i < static_cast<int>(boardBlackPreviews.size()) &&
            boardWhitePreviews[i].getSize().x > 0 &&
            boardBlackPreviews[i].getSize().x > 0) {
            const float previewSize = 32.f;

            sf::Sprite whiteTile(boardWhitePreviews[i]);
            auto whiteSize = boardWhitePreviews[i].getSize();

            whiteTile.setScale({
                previewSize / whiteSize.x,
                previewSize / whiteSize.y
            });

            whiteTile.setPosition({x + 12.f, y + 25.f});
            window.draw(whiteTile);

            sf::Sprite blackTile(boardBlackPreviews[i]);
            auto blackSize = boardBlackPreviews[i].getSize();

            blackTile.setScale({
                previewSize / blackSize.x,
                previewSize / blackSize.y
            });

            blackTile.setPosition({x + 46.f, y + 25.f});
            window.draw(blackTile);
        }
    }
}

void ChessUI::drawEndScreen() {
    sf::RectangleShape dimmer({1200.f, 800.f});
    dimmer.setPosition({0.f, 0.f});
    dimmer.setFillColor(sf::Color(0, 0, 0, 155));
    window.draw(dimmer);

    sf::RectangleShape endBox({520.f, 430.f});
    endBox.setPosition({340.f, 180.f});
    endBox.setFillColor(sf::Color(45, 45, 45));
    window.draw(endBox);

    sf::Text title(font, "GAME OVER", 48);
    title.setPosition({465.f, 235.f});
    title.setFillColor(sf::Color::White);
    window.draw(title);

    sf::Text message(font, "The game has ended.", 24);
    message.setPosition({477.f, 330.f});
    message.setFillColor(sf::Color(210, 210, 210));
    window.draw(message);

    sf::RectangleShape playAgain({200.f, 65.f});
    playAgain.setPosition({390.f, 500.f});
    playAgain.setFillColor(sf::Color(70, 120, 75));
    window.draw(playAgain);

    sf::Text playAgainText(font, "PLAY AGAIN", 20);
    playAgainText.setPosition({435.f, 520.f});
    playAgainText.setFillColor(sf::Color::White);
    window.draw(playAgainText);

    sf::RectangleShape settings({200.f, 65.f});
    settings.setPosition({610.f, 500.f});
    settings.setFillColor(sf::Color(65, 65, 65));
    window.draw(settings);

    sf::Text settingsText(font, "SETTINGS", 20);
    settingsText.setPosition({665.f, 520.f});
    settingsText.setFillColor(sf::Color::White);
    window.draw(settingsText);

    sf::RectangleShape menu({200.f, 60.f});
    menu.setPosition({500.f, 590.f});
    menu.setFillColor(sf::Color(55, 55, 55));
    window.draw(menu);

    sf::Text menuText(font, "MAIN MENU", 18);
    menuText.setPosition({550.f, 608.f});
    menuText.setFillColor(sf::Color::White);
    window.draw(menuText);
}

void ChessUI::drawCapturedPieces() {
    std::vector<int> capturedByWhite;
    std::vector<int> capturedByBlack;

    for (const auto& move : moveHistory) {
        if (move.capturedPiece == 0)
            continue;

        if (move.capturedPiece < 0)
            capturedByWhite.push_back(move.capturedPiece);
        else
            capturedByBlack.push_back(move.capturedPiece);
    }

    sf::Text whiteLabel(font, "White captured:", 14);
    whiteLabel.setPosition({50.f, 726.f});
    whiteLabel.setFillColor(sf::Color::White);
    window.draw(whiteLabel);

    sf::Text blackLabel(font, "Black captured:", 14);
    blackLabel.setPosition({50.f, 760.f});
    blackLabel.setFillColor(sf::Color::White);
    window.draw(blackLabel);

    auto drawCapturedRow = [&](const std::vector<int>& pieces, float y) {
        float x = 175.f;

        for (int piece : pieces) {
            sf::Sprite sprite(pieceTextures[piece]);

            auto textureSize = pieceTextures[piece].getSize();
            float maxSize = static_cast<float>(std::max(textureSize.x, textureSize.y));
            float scale = 25.f / maxSize;

            sprite.setScale({scale, scale});

            float width = textureSize.x * scale;
            float height = textureSize.y * scale;

            sprite.setPosition({x, y + (25.f - height) / 2.f});
            window.draw(sprite);

            x += width + 4.f;
        }
    };

    drawCapturedRow(capturedByWhite, 724.f);
    drawCapturedRow(capturedByBlack, 758.f);
}

void ChessUI::drawCoordinates() {
    for (int col = 0; col < 8; col++) {
        char fileLetter = static_cast<char>('a' + col);

        sf::Text fileText(font, std::string(1, fileLetter), 13);

        int shownCol = displayCol(col);

        fileText.setPosition({
            static_cast<float>(boardX + shownCol * squareSize + squareSize - 14),
            static_cast<float>(boardY + boardSize - 18)
        });

        fileText.setFillColor(sf::Color(35, 35, 35, 210));
        window.draw(fileText);
    }

    for (int row = 0; row < 8; row++) {
        char rankNumber = static_cast<char>('8' - row);

        sf::Text rankText(font, std::string(1, rankNumber), 13);

        int shownRow = displayRow(row);

        rankText.setPosition({
            static_cast<float>(boardX + 5),
            static_cast<float>(boardY + shownRow * squareSize + 2)
        });

        rankText.setFillColor(sf::Color(35, 35, 35, 210));
        window.draw(rankText);
    }
}

void ChessUI::playSound(const sf::SoundBuffer& buffer) {
    if (!soundEffects)
        return;

    if (buffer.getDuration() == sf::Time::Zero)
        return;

    activeSound.emplace(buffer);
    activeSound->play();
}

void ChessUI::drawBoardPosition(Board& board) {
    drawBoard();

    if (showCoordinates)
        drawCoordinates();

    int row = 0;

    for (auto& boardRow : board) {
        int col = 0;

        for (auto& piece : boardRow) {
            if (piece != 0) {
                sf::Sprite sprite(pieceTextures[piece]);

                auto textureSize = pieceTextures[piece].getSize();
                float maxSize = static_cast<float>( std::max(textureSize.x, textureSize.y) );
                float scale = 64.f / maxSize;

                sprite.setScale({scale, scale});

                float width = textureSize.x * scale;
                float height = textureSize.y * scale;

                int shownCol = displayCol(col);
                int shownRow = displayRow(row);

                float x = boardX + shownCol * squareSize + (squareSize - width) / 2.f;
                float y = boardY + shownRow * squareSize + (squareSize - height) / 2.f;

                sprite.setPosition({x, y});
                window.draw(sprite);
            }

            col++;
        }

        row++;
    }
}

void ChessUI::drawMoveArrow(const MoveHistoryEntry& move) {
    float startArrowX =
        boardX + displayCol(move.fromCol) * squareSize + squareSize / 2.f;
    float startArrowY =
        boardY + displayRow(move.fromRow) * squareSize + squareSize / 2.f;

    float endArrowX =
        boardX + displayCol(move.toCol) * squareSize + squareSize / 2.f;
    float endArrowY =
        boardY + displayRow(move.toRow) * squareSize + squareSize / 2.f;

    float dx = endArrowX - startArrowX;
    float dy = endArrowY - startArrowY;

    float length = std::sqrt(dx * dx + dy * dy);
    float angle = std::atan2(dy, dx) * 180.f / 3.14159265f;

    const float arrowThickness = 7.f;
    const float arrowHeadLength = 24.f;

    sf::RectangleShape arrowLine({length, arrowThickness});
    arrowLine.setOrigin({0.f, arrowThickness / 2.f});
    arrowLine.setPosition({startArrowX, startArrowY});
    arrowLine.setRotation(sf::degrees(angle));
    arrowLine.setFillColor(sf::Color(220, 40, 40, 220));
    window.draw(arrowLine);

    sf::RectangleShape arrowHead1({arrowHeadLength, arrowThickness});
    arrowHead1.setOrigin({0.f, arrowThickness / 2.f});
    arrowHead1.setPosition({endArrowX, endArrowY});
    arrowHead1.setRotation(sf::degrees(angle + 150.f));
    arrowHead1.setFillColor(sf::Color(220, 40, 40, 220));
    window.draw(arrowHead1);

    sf::RectangleShape arrowHead2({arrowHeadLength, arrowThickness});
    arrowHead2.setOrigin({0.f, arrowThickness / 2.f});
    arrowHead2.setPosition({endArrowX, endArrowY});
    arrowHead2.setRotation(sf::degrees(angle - 150.f));
    arrowHead2.setFillColor(sf::Color(220, 40, 40, 220));
    window.draw(arrowHead2);
}

std::string ChessUI::moveToText(const MoveHistoryEntry& move) const {
    auto squareName = [](int row, int col) {
        std::string square;
        square += static_cast<char>('a' + col);
        square += static_cast<char>('8' - row);
        return square;
    };

    char pieceLetter = ' ';

    int piece = std::abs(move.piece);

    if (piece == 2) pieceLetter = 'N';
    if (piece == 3) pieceLetter = 'B';
    if (piece == 4) pieceLetter = 'R';
    if (piece == 5) pieceLetter = 'Q';
    if (piece == 6) pieceLetter = 'K';

    std::string text;

    if (pieceLetter != ' ')
        text += pieceLetter;

    text += squareName(move.fromRow, move.fromCol);

    text += move.capturedPiece != 0 ? "x" : "-";

    text += squareName(move.toRow, move.toCol);

    return text;
}

void ChessUI::rebuildLinearHistoryFromNode(int nodeIndex) {
    moveHistory.clear();

    if (nodeIndex < 0) {
        currentHistoryIndex = -1;
        return;
    }

    std::vector<int> path;
    int node = nodeIndex;

    while (node >= 0) {
        path.push_back(node);
        node = historyTree[node].parent;
    }

    std::reverse(path.begin(), path.end());

    for (int index : path)
        moveHistory.push_back(historyTree[index].move);

    currentHistoryIndex = static_cast<int>(moveHistory.size()) - 1;

    const float spacing = 70.f;
    const float visibleHeight = sidePanelHeight - 90.f;

    int maxDepth = -1;

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        maxDepth = std::max(maxDepth, getHistoryDepth(i));
    }

    const float contentHeight = (maxDepth + 1) * spacing;
    const float maxScroll = std::max(0.f, contentHeight - visibleHeight);

    if (historyScroll > maxScroll)
        historyScroll = maxScroll;
}

void ChessUI::selectPiece(int row, int col) {
    pieceSelected = true;
    selectedRow = row;
    selectedCol = col;

    legalMoves.clear();

    if (!showPossibleMoves)
        return;

    for (int newRow = 0; newRow < 8; newRow++) {
        for (int newCol = 0; newCol < 8; newCol++) {
            if (game.valid_move(selectedRow, selectedCol, newRow, newCol)) {
                legalMoves.push_back({newRow, newCol});
            }
        }
    }
}

void ChessUI::clearSelection() {
    pieceSelected = false;
    selectedRow = -1;
    selectedCol = -1;

    legalMoves.clear();
}

void ChessUI::startGame(int engineSide, GameMode mode) {
    configuredEngineSide = engineSide;
    game = Board();
    startingBoard = game;

    gameMode = mode;
    gameStarted = true;
    currentScreen = Screen::Game;

    moveHistory.clear();
    historyTree.clear();
    currentHistoryNode = -1;

    legalMoves.clear();
    clearSelection();

    currentHistoryIndex = -1;
    historyScroll = 0.f;
    historyHorizontalScroll = 0.f;
    hoveredHistoryNode = -1;

    dragging = false;
    draggedRow = -1;
    draggedCol = -1;

    engineStalled = false;
    engineFrameDelay = 0;
    historyPaused = false;
}

Engine& ChessUI::engineForTurn() {
    return game.get_current_turn() == 0 ? whiteEngine : blackEngine;
}

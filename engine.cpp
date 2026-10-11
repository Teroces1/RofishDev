#include <string>
#include <vector>
#include <unordered_map>
#include <random>
#include <cctype>
#include <iostream>
#include <chrono>
#include <algorithm>

#include "engine.hpp"

namespace Chess {
    const int kingSquareBonus[64] = {
        20, 25,  0, -5, -5,  0, 25, 20,
        20, 20,  0,  0,  0,  0, 20, 20,
       -10,-20,-20,-20,-20,-20,-20,-10,
       -20,-30,-30,-40,-40,-30,-30,-20,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30
    };

    const int kingSquareEndgameBonus[64] = {
       -50,-30,-30,-30,-30,-30,-30,-50,
       -30,-30,  0,  0,  0,  0,-30,-30,
       -30,-10, 20, 30, 30, 20,-10,-30,
       -30,-10, 30, 40, 40, 30,-10,-30,
       -30,-10, 30, 40, 40, 30,-10,-30,
       -30,-10, 20, 30, 30, 20,-10,-30,
       -30,-20,-10,  0,  0,-10,-20,-30,
       -50,-40,-30,-20,-20,-30,-40,-50
    };

    const int pawnSquareBonus[64] = {
         0,  0,  0,  0,  0,  0,  0,  0,
         5, 10, 10,-10,-10, 10, 10,  5,
         5, -5,-10,  0,  0,-10, -5,  5,
         0,  0,  0, 25, 25,  0,  0,  0,
         5,  5, 12, 28, 28, 12,  5,  5,
        10, 10, 20, 30, 30, 20, 10, 10,
        55, 55, 55, 55, 55, 55, 55, 55, // 85-30
         0,  0,  0,  0,  0,  0,  0,  0
    };

    const int pawnSquareEndgameBonus[64] = {
         0,  0,  0,  0,  0,  0,  0,  0,
         5,  5,  5,  5,  5,  5,  5,  5,
         5,  5,  5,  5,  5,  5,  5,  5,
        15, 15, 15, 15, 15, 15, 15, 15,
        30, 30, 30, 30, 30, 30, 30, 30,
        45, 45, 45, 45, 45, 45, 45, 45,
        60, 60, 60, 60, 60, 60, 60, 60, // 90-30
         0,  0,  0,  0,  0,  0,  0,  0
    };

    const int piecePositionBonuses[8][64] = {
        {0}, {0}, // 0 and 1 (King) unused here
        { // 2: Knight
           -50,-40,-30,-30,-30,-30,-40,-50,
           -40,-20,  0,  0,  0,  0,-20,-40,
           -30,  0, 10, 15, 15, 10,  0,-30,
           -30,  5, 15, 20, 20, 15,  5,-30,
           -30,  0, 15, 20, 20, 15,  0,-30,
           -30,  5, 10, 15, 15, 10,  5,-30,
           -40,-20,  0,  5,  5,  0,-20,-40,
           -50,-40,-30,-30,-30,-30,-40,-50
        },
        { // 3: Pawn
             0,  0,  0,  0,  0,  0,  0,  0,
             5, 10, 10,-40,-40, 10, 10,  5,
             5, -5,-10,  0,  0,-10, -5,  5,
             0,  0,  0, 25, 25,  0,  0,  0,
             5,  5, 12, 28, 28, 12,  5,  5,
            15, 10, 20, 30, 30, 20, 10, 15,
            50, 50, 50, 50, 50, 50, 50, 50,
             0,  0,  0,  0,  0,  0,  0,  0
        },
        {0}, // 4 unused
        { // 5: Rook
             0,  0,  0,  5,  5,  0,  0,  0,
            -5,  0,  0,  0,  0,  0,  0, -5,
            -5,  0,  0,  0,  0,  0,  0, -5,
            -5,  0,  0,  0,  0,  0,  0, -5,
            -5,  0,  0,  0,  0,  0,  0, -5,
            -5,  0,  0,  0,  0,  0,  0, -5,
             5, 10, 10, 10, 10, 10, 10,  5,
             0,  0,  0,  0,  0,  0,  0,  0
        },
        { // 6: Bishop
           -20,-10,-10,-10,-10,-10,-10,-20,
           -10,  5,  0,  0,  0,  0,  5,-10,
           -10, 10, 10, 10, 10, 10, 10,-10,
           -10,  0, 10, 10, 10, 10,  0,-10,
           -10,  5,  5, 10, 10,  5,  5,-10,
           -10,  0,  5, 10, 10,  5,  0,-10,
           -10,  0,  0,  0,  0,  0,  0,-10,
           -20,-10,-10,-10,-10,-10,-10,-20
        },
        { // 7: Queen
           -20,-10,-10, -5, -5,-10,-10,-20,
           -10,  0,  5,  0,  0,  5,  0,-10,
           -10,  5,  5,  5,  5,  5,  5,-10,
             0,  0,  5,  5,  5,  5,  0,  0,
            -5,  0,  5,  5,  5,  5,  0, -5,
           -10,  0,  5,  5,  5,  5,  0,-10,
           -10,  0,  0,  0,  0,  0,  0,-10,
           -20,-10,-10, -5, -5,-10,-10,-20
        }
    };

    const int EVAL_TEMPO_BONUS = 13;
    const int EVAL_ENDGAMETEMPO_BONUS = 7;
    const int EVAL_EXPECTED_MOBILITY[8] = {0, 0, 4, 0, 0, 8, 7, 13};
    const int EVAL_MOBILITY_WEIGHTS[8] = {0, 0, 2, 0, 0, 2, 2, 1};
    
    // Commented out features constants
    const int EVAL_PAWNSHIELD_BONUS = 10;
    const int EVAL_PAWNSHIELDRANK_PENALTY = 5;
    const int EVAL_EMPTYENEMYFILE_PENALTY = 12;
    const int EVAL_EMPTYFRIENDLYFILE_PENALTY = 18;
    const int EVAL_OPENKINGFILE_PENALTY = 10;
    const int EVAL_DOUBLEDPAWN_PENALTY = 5;
    const int EVAL_ISOLATEDPAWN_PENALTY = 20;
    const int EVAL_PASSEDPAWN_BONUS[9] = {0, 0, 10, 10, 20, 35, 50, 50, 0};
    const int EVAL_PROTECTEDPASSEDPAWN_BONUS = 20;
    const int EVAL_BISHOPPAIR_BONUS = 30;
    const int EVAL_CASTLINGRIGHTS_BONUS = 10;
    const int EVAL_BACKWARDSPAWN_PENALTY = 8;
    const int EVAL_BACKWARDSPAWNOPEN_PENALTY = 18;

    const int gamePhaseValues[16] = {
        0, 0, 1, 0, 0, 2, 1, 4,
        0, 0, 1, 0, 0, 2, 1, 4
    };

    int endGameKingEval[4096];
    bool endGameKingEvalInitialized = false;

    void initEndGameKingEval() {
        if (endGameKingEvalInitialized) return;
        for (int i = 0; i < 64; ++i) {
            int opponentFile = i % 8;
            int opponentRank = i / 8;
            for (int j = 0; j < 64; ++j) {
                int kingFile = j % 8;
                int kingRank = j / 8;
                int distanceFile = std::abs(kingFile - opponentFile);
                int distanceRank = std::abs(kingRank - opponentRank);

                int distanceFromCenterFile = std::max(3 - opponentFile, opponentFile - 4);
                int distanceFromCenterRank = std::max(3 - opponentRank, opponentRank - 4);

                endGameKingEval[i + j * 64] = 4 * (14 - (distanceFile + distanceRank)) + 10 * (distanceFromCenterFile + distanceFromCenterRank);
            }
        }
        endGameKingEvalInitialized = true;
    }




    double Engine::LMR_LOG[65];
    bool Engine::lmrInitialized = false;

    double Engine::getClock() {
        using namespace std::chrono;
        return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
    }

    Engine::Engine() {
        Magics::init();

        for (int i = 0; i < 16; ++i) PieceValues[i] = 0;
        
        PieceValues[PIECE_KING] = 10000;
        PieceValues[PIECE_KNIGHT] = 310;
        PieceValues[PIECE_PAWN] = 100;
        PieceValues[PIECE_ROOK] = 500;
        PieceValues[PIECE_BISHOP] = 330;
        PieceValues[PIECE_QUEEN] = 900;

        PieceValues[PIECE_KING + 8] = 10000;
        PieceValues[PIECE_KNIGHT + 8] = 310;
        PieceValues[PIECE_PAWN + 8] = 100;
        PieceValues[PIECE_ROOK + 8] = 500;
        PieceValues[PIECE_BISHOP + 8] = 330;
        PieceValues[PIECE_QUEEN + 8] = 900;


        initializeTranspositionTable(21);
        initializeZobristKeys();

        if (!lmrInitialized) {
            for (int i = 0; i <= 64; ++i) {
                LMR_LOG[i] = std::log(std::max(1, i));
            }
            lmrInitialized = true;
        }
        initEndGameKingEval();
    }

    

    MoveGenResult Engine::getLegalMoves(bool noSort, int ply, bool onlyCaptures) {
        int us = sideToMove == 8 ? 1 : 0;
        int them = 1 - us;

        uint64_t myPieces = colorBB[us];
        uint64_t enemyPieces = colorBB[them];
        uint64_t allPieces = myPieces | enemyPieces;

        int myKing = (us == 1) ? lightKing : darkKing;
        uint64_t myKingBB = 1ULL << myKing;

        // 1. Calculate Enemy Attacks & Checkers
        uint64_t enemyAttacks = 0;
        uint64_t checkers = 0;
        uint64_t pawnAttacks = 0;

        uint64_t enemyPawns = pieceBB[them][PIECE_PAWN];
        if (us == 1) { // White to move, Black pawns attack downwards
            pawnAttacks |= (enemyPawns >> 7) & ~FILE_A;
            pawnAttacks |= (enemyPawns >> 9) & ~FILE_H;
            if (pawnAttacks & myKingBB) checkers |= enemyPawns & Moves::lightPawnAttack[myKing];
        } else {       // Black to move, White pawns attack upwards
            pawnAttacks |= (enemyPawns << 7) & ~FILE_H;
            pawnAttacks |= (enemyPawns << 9) & ~FILE_A;
            if (pawnAttacks & myKingBB) checkers |= enemyPawns & Moves::darkPawnAttack[myKing];
        }
        enemyAttacks |= pawnAttacks;
        // std::cout << "pawns: " << enemyAttacks << std::endl;
        
            // std::cout << "checking pieces1: " << checkers << std::endl;

        uint64_t k = pieceBB[them][PIECE_KNIGHT];
        while (k) {
            int sq = __builtin_ctzll(k); k &= k - 1;
            uint64_t att = Moves::knightAttacks[sq];
            enemyAttacks |= att;
            if (att & myKingBB) checkers |= (1ULL << sq);
        }
            // std::cout << "checking pieces2: " << checkers << std::endl;

        enemyAttacks |= Moves::kingAttacks[(us == 1) ? darkKing : lightKing];

        uint64_t enemyRQ = pieceBB[them][PIECE_ROOK] | pieceBB[them][PIECE_QUEEN];
        uint64_t enemyBQ = pieceBB[them][PIECE_BISHOP] | pieceBB[them][PIECE_QUEEN];
        uint64_t piecesNoKing = allPieces ^ myKingBB;

        uint64_t rq = enemyRQ;
        while (rq) {
            int sq = __builtin_ctzll(rq); rq &= rq - 1;
            uint64_t att = Magics::getRookAttacks(sq, piecesNoKing);
            enemyAttacks |= att;
            if (att & myKingBB) checkers |= (1ULL << sq);
        }
            // std::cout << "checking pieces3: " << checkers << std::endl;

        uint64_t bq = enemyBQ;
        while (bq) {
            int sq = __builtin_ctzll(bq); bq &= bq - 1;
            uint64_t att = Magics::getBishopAttacks(sq, piecesNoKing);
            enemyAttacks |= att;
            if (att & myKingBB) checkers |= (1ULL << sq);
        }

        bool incheck = checkers != 0;

        // if (incheck) {
        //     std::cout << "checking pieces: " << checkers << std::endl;
        // }

        // 2. Calculate Pins
        uint64_t pinned = 0;
        uint64_t pinMask[64] = {0};

        uint64_t pinnerRooks = Magics::getRookAttacks(myKing, enemyPieces) & enemyRQ;
        while (pinnerRooks) {
            int sq = __builtin_ctzll(pinnerRooks); pinnerRooks &= pinnerRooks - 1;
            uint64_t ray = Magics::rayBetween[myKing][sq];
            uint64_t blockers = ray & myPieces;
            if (__builtin_popcountll(blockers) == 1) {
                int pSq = __builtin_ctzll(blockers);
                pinned |= (1ULL << pSq);
                pinMask[pSq] = ray;
            }
        }

        uint64_t pinnerBishops = Magics::getBishopAttacks(myKing, enemyPieces) & enemyBQ;
        while (pinnerBishops) {
            int sq = __builtin_ctzll(pinnerBishops); pinnerBishops &= pinnerBishops - 1;
            uint64_t ray = Magics::rayBetween[myKing][sq];
            uint64_t blockers = ray & myPieces;
            if (__builtin_popcountll(blockers) == 1) {
                int pSq = __builtin_ctzll(blockers);
                pinned |= (1ULL << pSq);
                pinMask[pSq] = ray;
            }
        }

        // 3. Setup Masks
        uint64_t checkMask = ~0ULL;
        if (incheck) {
            if (__builtin_popcountll(checkers) > 1) checkMask = 0; // Double check: only king moves
            else checkMask = Magics::rayBetween[myKing][__builtin_ctzll(checkers)];
        }

        // std::cout << "check mask: " << checkMask << " | king: " << myKing << " | target: " << __builtin_ctzll(checkers) << " | ray: " << Magics::rayBetween[myKing][__builtin_ctzll(checkers)] << std::endl;

        uint64_t captureMask = onlyCaptures ? enemyPieces : ~0ULL;
        uint64_t enPassantTargetBB = (enPassantTarget != 64) ? (1ULL << enPassantTarget) : 0;

        int moveCount = 0;
        uint32_t* results = moveBuffer[ply];
        int* moveScores = scoreBuffer[ply];
        uint32_t hashedMove = TTgetMove();

        // Helper lambda for exact 1:1 scoring of non-pawn moves
        auto addNormalMove = [&](int sq, int c) __attribute__((always_inline)) {
            int piece = board[sq] % 8;
            int score = 0;
            if (board[c] > 0) score += 1000 + 10 * PieceValues[board[c]] - PieceValues[piece];
            if (pawnAttacks & (1ULL << c)) score -= PieceValues[piece];
            score += piecePositionBonuses[piece][c] - piecePositionBonuses[piece][sq];
            score += history[piece * 64 + c];
            
            int move = sq + c * 64;
            if (board[c] > 0) {
                score += (SEE(move) >= 0) ? SCORE_GOODCAPTURE : SCORE_BADCAPTURE;
            }
            
            results[moveCount] = move;
            moveScores[moveCount] = score;
            
            if (!noSort) {
                if (results[moveCount] == hashedMove) moveScores[moveCount] += SCORE_HASHED;
                else if (results[moveCount] == killerMoves[ply * 2]) moveScores[moveCount] += SCORE_KILLER1;
                else if (results[moveCount] == killerMoves[ply * 2 + 1]) moveScores[moveCount] += SCORE_KILLER2;
            }
            moveCount++;
        };

        // 4. Generate Moves
        
        // King
        uint64_t kingMoves = Moves::kingAttacks[myKing] & ~myPieces & ~enemyAttacks & captureMask;
        while (kingMoves) {
            int c = __builtin_ctzll(kingMoves); kingMoves &= kingMoves - 1;
            addNormalMove(myKing, c);
        }

        // Castling (Luau exactly translates indices 60 and 4 to 59 and 3)
        if (!incheck && !onlyCaptures) {
            uint64_t kMoves = Moves::kingMoves[myKing] & ~enemyAttacks & ~myPieces;
            uint64_t rights = Moves::castlingRightsMask[castlingRights];

            if (us == 0 && myKing == 60) {
                uint64_t qsSafe = (1ULL << 58) | (1ULL << 59);
                if ((rights & qsSafe) == qsSafe && (kMoves & qsSafe) == qsSafe && !(allPieces & (qsSafe | (1ULL << 57)))) {
                    results[moveCount] = 48828; moveScores[moveCount] = history[59]; moveCount++;
                }
                uint64_t ksSafe = (1ULL << 61) | (1ULL << 62);
                if ((rights & ksSafe) == ksSafe && (kMoves & ksSafe) == ksSafe && !(allPieces & ksSafe)) {
                    results[moveCount] = 53180; moveScores[moveCount] = history[63]; moveCount++;
                }
            } else if (us == 1 && myKing == 4) {
                uint64_t qsSafe = (1ULL << 2) | (1ULL << 3);
                if ((rights & qsSafe & kMoves) == qsSafe && !(allPieces & (qsSafe | (1ULL << 1)))) {
                    results[moveCount] = 45188; moveScores[moveCount] = history[578]; moveCount++;
                }
                uint64_t ksSafe = (1ULL << 5) | (1ULL << 6);
                if ((rights & ksSafe) == ksSafe && (kMoves & ksSafe) == ksSafe && !(allPieces & ksSafe)) {
                    results[moveCount] = 49540; moveScores[moveCount] = history[582]; moveCount++;
                }
            }
        }

        // Double Check Optimization: If double check, limitMovesForChecks becomes 0 in Luau, 
        // effectively generating 0 moves for all other pieces. We can just return early here!
        if (__builtin_popcountll(checkers) > 1) return {moveCount, incheck};

        // Knights
        uint64_t knights = pieceBB[us][PIECE_KNIGHT] & ~pinned;
        while (knights) {
            int sq = __builtin_ctzll(knights); knights &= knights - 1;
            uint64_t moves = Moves::knightAttacks[sq] & ~myPieces & checkMask & captureMask;
            while (moves) {
                int c = __builtin_ctzll(moves); moves &= moves - 1;
                addNormalMove(sq, c);
            }
        }

        // Rooks & Queens
        uint64_t rooksQueens = pieceBB[us][PIECE_ROOK] | pieceBB[us][PIECE_QUEEN];
        while (rooksQueens) {
            int sq = __builtin_ctzll(rooksQueens); rooksQueens &= rooksQueens - 1;
            uint64_t moves = Magics::getRookAttacks(sq, allPieces) & ~myPieces & checkMask & captureMask;
            if (pinned & (1ULL << sq)) moves &= pinMask[sq];
            while (moves) {
                int c = __builtin_ctzll(moves); moves &= moves - 1;
                addNormalMove(sq, c);
            }
        }

        // Bishops & Queens
        uint64_t bishopsQueens = pieceBB[us][PIECE_BISHOP] | pieceBB[us][PIECE_QUEEN];
        while (bishopsQueens) {
            int sq = __builtin_ctzll(bishopsQueens); bishopsQueens &= bishopsQueens - 1;
            uint64_t moves = Magics::getBishopAttacks(sq, allPieces) & ~myPieces & checkMask & captureMask;
            if (pinned & (1ULL << sq)) moves &= pinMask[sq];
            while (moves) {
                int c = __builtin_ctzll(moves); moves &= moves - 1;
                addNormalMove(sq, c);
            }
        }

        // Pawns
        uint64_t pawns = pieceBB[us][PIECE_PAWN];
        uint64_t pawnValid = captureMask;
        if (onlyCaptures) {
            pawnValid |= enPassantTargetBB;
            pawnValid |= (us == 1) ? 0xFF00000000000000ULL : 0x00000000000000FFULL; // Allow promotions
        }

        while (pawns) {
            int sq = __builtin_ctzll(pawns); pawns &= pawns - 1;
            uint64_t moves = 0;

            if (us == 1) { // White
                uint64_t push = (1ULL << (sq + 8)) & ~allPieces;
                if (push && sq <= 15) push |= (1ULL << (sq + 16)) & ~allPieces;
                moves = push | (Moves::lightPawnAttack[sq] & (enemyPieces | enPassantTargetBB));
            } else {       // Black
                uint64_t push = (1ULL << (sq - 8)) & ~allPieces;
                if (push && sq >= 48) push |= (1ULL << (sq - 16)) & ~allPieces;
                moves = push | (Moves::darkPawnAttack[sq] & (enemyPieces | enPassantTargetBB));
            }

            if (pinned & (1ULL << sq)) moves &= pinMask[sq];
            
            uint64_t epMove = moves & enPassantTargetBB;

            // std::cout << "generating moves for square " << squareToAlgebraic(sq) << " | current moves: " << moves << std::endl;
            
            if (incheck) {
                if (enPassantTarget != 64) {
                    int epCapSq = (us == 1) ? enPassantTarget - 8 : enPassantTarget + 8;
                    if (checkers == (1ULL << epCapSq)) {
                        moves &= (checkMask | enPassantTargetBB); // En Passant removes the checking pawn
                    } else {
                        moves &= checkMask;
                    }
                } else {
                    moves &= checkMask;
                }
            }

            // std::cout << "generating moves for square " << squareToAlgebraic(sq) << " | new moves: " << moves << std::endl;

            if (epMove && (moves & enPassantTargetBB)) {
                // Fast En Passant Validation
                int epCapSq = (us == 1) ? enPassantTarget - 8 : enPassantTarget + 8;
                uint64_t occ = (allPieces ^ (1ULL << sq) ^ (1ULL << epCapSq)) | (1ULL << enPassantTarget);
                bool epSafe = true;

                if (Magics::getRookAttacks(myKing, occ) & enemyRQ) epSafe = false;
                else if (Magics::getBishopAttacks(myKing, occ) & enemyBQ) epSafe = false;
                
                if (!epSafe) moves &= ~enPassantTargetBB;
            }

            moves &= pawnValid;

            while (moves) {
                int c = __builtin_ctzll(moves); moves &= moves - 1;

                int score = 0;
                if (board[c] > 0) score += 1000 + 10 * PieceValues[board[c]] - PieceValues[PIECE_PAWN];
                if (pawnAttacks & (1ULL << c)) score -= PieceValues[PIECE_PAWN];
                
                score += piecePositionBonuses[PIECE_PAWN][c] - piecePositionBonuses[PIECE_PAWN][sq];
                score += history[PIECE_PAWN * 64 + c];

                int move = sq + c * 64;

                if (board[c] > 0) {
                    // SEE call exactly matches Luau: Queen promo flag if promotion, else normal move
                    int tradeValue = SEE((c <= 7 || c >= 56) ? (move | (7 << 12)) : move);
                    score += (tradeValue >= 0) ? SCORE_GOODCAPTURE : SCORE_BADCAPTURE;
                }

                if (c <= 7 || c >= 56) { // Promotion
                    if (onlyCaptures) {
                        if (board[c] != 0) {
                            // Exact Luau array layout for captures
                            results[moveCount] = move | (7 << 12);     // Queen
                            moveScores[moveCount] = score + 900;
                            results[moveCount + 1] = move | (2 << 12); // Knight
                            moveScores[moveCount + 1] = score + 300;
                            
                            if (!noSort) {
                                if (results[moveCount] == hashedMove) moveScores[moveCount] += SCORE_HASHED;
                                else if (results[moveCount + 1] == hashedMove) moveScores[moveCount + 1] += SCORE_HASHED;
                            }
                            moveCount += 2;
                        } else {
                            results[moveCount] = move | (7 << 12);
                            moveScores[moveCount] = score + 900;
                            if (!noSort && results[moveCount] == hashedMove) moveScores[moveCount] += SCORE_HASHED;
                            moveCount += 1;
                        }
                    } else {
                        // Exact Luau array layout for normal promotions
                        results[moveCount] = move | (7 << 12);     // Queen
                        moveScores[moveCount] = score + 900;
                        results[moveCount + 1] = move | (6 << 12); // Bishop
                        moveScores[moveCount + 1] = score + 320;
                        results[moveCount + 2] = move | (5 << 12); // Rook
                        moveScores[moveCount + 2] = score + 500;
                        results[moveCount + 3] = move | (2 << 12); // Knight
                        moveScores[moveCount + 3] = score + 300;

                        if (!noSort) {
                            if (results[moveCount] == hashedMove) moveScores[moveCount] += SCORE_HASHED;
                            else if (results[moveCount + 1] == hashedMove) moveScores[moveCount + 1] += SCORE_HASHED;
                            else if (results[moveCount + 2] == hashedMove) moveScores[moveCount + 2] += SCORE_HASHED;
                            else if (results[moveCount + 3] == hashedMove) moveScores[moveCount + 3] += SCORE_HASHED;
                        }
                        moveCount += 4;
                    }
                } else if (c == enPassantTarget) { // En Passant
                    results[moveCount] = move | (1 << 12);
                    moveScores[moveCount] = score;
                    if (!noSort) {
                        if (results[moveCount] == hashedMove) moveScores[moveCount] += SCORE_HASHED;
                        else if (results[moveCount] == killerMoves[ply * 2]) moveScores[moveCount] += SCORE_KILLER1;
                        else if (results[moveCount] == killerMoves[ply * 2 + 1]) moveScores[moveCount] += SCORE_KILLER2;
                    }
                    moveCount += 1;
                } else { // Normal Pawn Move
                    results[moveCount] = move;
                    moveScores[moveCount] = score;
                    if (!noSort) {
                        if (results[moveCount] == hashedMove) moveScores[moveCount] += SCORE_HASHED;
                        else if (results[moveCount] == killerMoves[ply * 2]) moveScores[moveCount] += SCORE_KILLER1;
                        else if (results[moveCount] == killerMoves[ply * 2 + 1]) moveScores[moveCount] += SCORE_KILLER2;
                    }
                    moveCount += 1;
                }
            }
        }

        return {moveCount, incheck};
    }

    void Engine::initializeZobristKeys() {
        std::mt19937_64 rng(12331); // Same seed as your Luau implementation

        for (int i = 0; i < 64; ++i) {
            for (int j = 0; j < 16; ++j) {
                pieceZobristKeys[i][j] = rng();
            }
        }
        for (int i = 0; i < 16; ++i) {
            castlingRightsZobristKeys[i] = rng();
        }
        for (int i = 0; i < 65; ++i) {
            enPassantZobristKeys[i] = rng();
        }
        sideToMoveZobristKey = rng();
    }

    void Engine::importZobristKeys(const std::string& json) {
        // Lightweight parser to extract all numbers from the JSON string
        std::vector<uint64_t> numbers;
        std::string current_num = "";
        for (char c : json) {
            if (isdigit(c)) {
                current_num += c;
            } else if (!current_num.empty()) {
                numbers.push_back(std::stoull(current_num));
                current_num = "";
            }
        }
        if (!current_num.empty()) numbers.push_back(std::stoull(current_num));

        // Populate the keys sequentially
        int idx = 0;
        for (int i = 0; i < 64; ++i) {
            for (int j = 0; j < 16; ++j) {
                pieceZobristKeys[i][j] = numbers[idx++];
            }
        }
        for (int i = 0; i < 16; ++i) castlingRightsZobristKeys[i] = numbers[idx++];
        for (int i = 0; i < 65; ++i) enPassantZobristKeys[i] = numbers[idx++];
        sideToMoveZobristKey = numbers[idx++];
    }

    void Engine::loadFEN(const std::string& fen) {
        for (int i = 0; i < 64; ++i) board[i] = 0;
        for (int i = 0; i < 2; ++i) {
            colorBB[i] = 0;
            for (int j = 0; j < 8; ++j) pieceBB[i][j] = 0;
        }

        int rank = 7, file = 0;
        size_t idx = 0;

        // 1. Piece placement
        while (idx < fen.length() && fen[idx] != ' ') {
            char c = fen[idx++];
            if (c == '/') {
                rank--; file = 0;
            } else if (isdigit(c)) {
                file += (c - '0');
            } else {
                int color = isupper(c) ? 1 : 0;
                int piece = 0;
                char lc = tolower(c);
                if (lc == 'k') piece = PIECE_KING;
                else if (lc == 'n') piece = PIECE_KNIGHT;
                else if (lc == 'p') piece = PIECE_PAWN;
                else if (lc == 'r') piece = PIECE_ROOK;
                else if (lc == 'b') piece = PIECE_BISHOP;
                else if (lc == 'q') piece = PIECE_QUEEN;

                int sq = rank * 8 + file;
                board[sq] = piece + (color == 1 ? 8 : 0);
                
                uint64_t bb = 1ULL << sq;
                colorBB[color] |= bb;
                pieceBB[color][piece] |= bb;

                if (piece == PIECE_KING) {
                    if (color == 1) lightKing = sq;
                    else darkKing = sq;
                }
                file++;
            }
        }

        // 2. Side to move
        idx++;
        sideToMove = (fen[idx] == 'w') ? 8 : 0;
        idx += 2;

        // 3. Castling rights
        castlingRights = 0;
        if (fen[idx] == '-') {
            idx++;
        } else {
            while (idx < fen.length() && fen[idx] != ' ') {
                if (fen[idx] == 'q') castlingRights |= 1;      // Black QS
                else if (fen[idx] == 'k') castlingRights |= 2; // Black KS
                else if (fen[idx] == 'Q') castlingRights |= 4; // White QS
                else if (fen[idx] == 'K') castlingRights |= 8; // White KS
                idx++;
            }
        }
        idx++;

        // 4. En Passant target
        if (fen[idx] == '-') {
            enPassantTarget = 64;
            idx++;
        } else {
            int epFile = fen[idx++] - 'a';
            int epRank = fen[idx++] - '1';
            enPassantTarget = epRank * 8 + epFile;
        }
        idx++;

        // 5. Halfmove clock
        if (idx < fen.length()) {
            size_t nextSpace = fen.find(' ', idx);
            halfTimeClock = std::stoi(fen.substr(idx, nextSpace - idx));
        } else {
            halfTimeClock = 0;
        }

        totalMoves = 0;

        // Calculate initial hash
        hash = 0;
        for (int i = 0; i < 64; ++i) {
            if (board[i] > 0) hash ^= pieceZobristKeys[i][board[i]];
        }
        hash ^= castlingRightsZobristKeys[castlingRights];
        hash ^= enPassantZobristKeys[enPassantTarget];
        if (sideToMove == 8) hash ^= sideToMoveZobristKey;

        // positionHistory.clear();
        // positionHistory[hash] = 1;
    }

    void Engine::makeMove(int move) {
        int moveFrom = move & 0x3F;
        int moveTo = (move >> 6) & 0x3F;
        int moveSpecial = (move >> 12) & 0xF;

        int piece = board[moveFrom];
        int capturedPiece = board[moveTo];
        int us = sideToMove == 8 ? 1 : 0;
        int them = 1 - us;
        int pieceType = piece % 8;
        
        // Save State
        UndoRecord& record = moveLog[totalMoves];
        record.move = move;
        record.capturedPiece = capturedPiece;
        record.castlingRights = castlingRights;
        record.enPassantTarget = enPassantTarget;
        record.halfTimeClock = halfTimeClock;
        record.colorBB[0] = colorBB[0];
        record.colorBB[1] = colorBB[1];
        record.hash = hash;

        halfTimeClock++;

        // Zobrist hash updates
        if (capturedPiece > 0) {
            halfTimeClock = 0;
            hash ^= pieceZobristKeys[moveTo][capturedPiece] ^ pieceZobristKeys[moveFrom][piece] ^ pieceZobristKeys[moveTo][piece] ^ sideToMoveZobristKey;
        } else {
            if (pieceType == PIECE_PAWN || moveSpecial == 11 || moveSpecial == 12) {
                halfTimeClock = 0;
            }
            hash ^= pieceZobristKeys[moveFrom][piece] ^ pieceZobristKeys[moveTo][piece] ^ sideToMoveZobristKey;
        }

        board[moveTo] = piece;
        board[moveFrom] = 0;

        uint64_t fromBB = 1ULL << moveFrom;
        uint64_t toBB = 1ULL << moveTo;
        uint64_t moveMask = fromBB | toBB;

        // Update Color Bitboards
        colorBB[us] ^= moveMask;
        if (capturedPiece > 0) {
            colorBB[them] ^= toBB;
            pieceBB[them][capturedPiece % 8] ^= toBB;
        }

        // Remove old EP hash
        if (enPassantTarget != 64) {
            hash ^= enPassantZobristKeys[64] ^ enPassantZobristKeys[enPassantTarget];
        }
        enPassantTarget = 64;

        if (moveSpecial == 0) { // Normal move
            pieceBB[us][pieceType] ^= moveMask;
            if (pieceType == PIECE_KING) {
                if (us == 0) darkKing = moveTo;
                else lightKing = moveTo;
            } else if (pieceType == PIECE_PAWN) {
                if (moveFrom - moveTo == 16) { // Black double push
                    enPassantTarget = moveFrom - 8;
                    hash ^= enPassantZobristKeys[64] ^ enPassantZobristKeys[enPassantTarget];
                } else if (moveTo - moveFrom == 16) { // White double push
                    enPassantTarget = moveFrom + 8;
                    hash ^= enPassantZobristKeys[64] ^ enPassantZobristKeys[enPassantTarget];
                }
            }
        } else if (moveSpecial == 1) { // En Passant
            pieceBB[us][PIECE_PAWN] ^= moveMask;
            int epSquare = (us == 0) ? moveTo + 8 : moveTo - 8;
            uint64_t epBB = 1ULL << epSquare;
            board[epSquare] = 0;
            
            pieceBB[them][PIECE_PAWN] ^= epBB;
            colorBB[them] ^= epBB;
            
            int capPawn = (us == 0) ? PIECE_PAWN + 8 : PIECE_PAWN;
            hash ^= pieceZobristKeys[epSquare][capPawn];
        } else if (moveSpecial == 11) { // Castle Queen Side
            pieceBB[us][PIECE_KING] ^= moveMask;
            if (us == 0) {
                board[56] = 0; board[59] = PIECE_ROOK;
                darkKing = 58;
                pieceBB[us][PIECE_ROOK] ^= (1ULL << 56) | (1ULL << 59);
                colorBB[us] ^= (1ULL << 56) | (1ULL << 59);
                hash ^= pieceZobristKeys[56][PIECE_ROOK] ^ pieceZobristKeys[59][PIECE_ROOK];
            } else {
                board[0] = 0; board[3] = PIECE_ROOK + 8;
                lightKing = 2;
                pieceBB[us][PIECE_ROOK] ^= (1ULL << 0) | (1ULL << 3);
                colorBB[us] ^= (1ULL << 0) | (1ULL << 3);
                hash ^= pieceZobristKeys[0][PIECE_ROOK + 8] ^ pieceZobristKeys[3][PIECE_ROOK + 8];
            }
        } else if (moveSpecial == 12) { // Castle King Side
            pieceBB[us][PIECE_KING] ^= moveMask;
            if (us == 0) {
                board[63] = 0; board[61] = PIECE_ROOK;
                darkKing = 62;
                pieceBB[us][PIECE_ROOK] ^= (1ULL << 63) | (1ULL << 61);
                colorBB[us] ^= (1ULL << 63) | (1ULL << 61);
                hash ^= pieceZobristKeys[63][PIECE_ROOK] ^ pieceZobristKeys[61][PIECE_ROOK];
            } else {
                board[7] = 0; board[5] = PIECE_ROOK + 8;
                lightKing = 6;
                pieceBB[us][PIECE_ROOK] ^= (1ULL << 7) | (1ULL << 5);
                colorBB[us] ^= (1ULL << 7) | (1ULL << 5);
                hash ^= pieceZobristKeys[7][PIECE_ROOK + 8] ^ pieceZobristKeys[5][PIECE_ROOK + 8];
            }
        } else { // Promotion
            int promoPiece = moveSpecial + sideToMove;
            board[moveTo] = promoPiece;
            hash ^= pieceZobristKeys[moveTo][piece] ^ pieceZobristKeys[moveTo][promoPiece];
            
            pieceBB[us][PIECE_PAWN] ^= fromBB;
            pieceBB[us][moveSpecial] ^= toBB;
        }

        uint8_t oldRights = castlingRights;
        castlingRights &= Moves::castlingRightsUpdater[moveFrom] & Moves::castlingRightsUpdater[moveTo];
        if (castlingRights != oldRights) {
            hash ^= castlingRightsZobristKeys[oldRights] ^ castlingRightsZobristKeys[castlingRights];
        }

        sideToMove = 8 - sideToMove;
        totalMoves++;
        // positionHistory[hash]++;
    }

    void Engine::unmakeMove() {
        // positionHistory[hash]--;
        // if (positionHistory[hash] == 0) {
        //     positionHistory.erase(hash);
        // }

        totalMoves--;
        sideToMove = 8 - sideToMove;
        int us = sideToMove == 8 ? 1 : 0;
        int them = 1 - us;

        // Restore State
        UndoRecord& record = moveLog[totalMoves];
        int move = record.move;
        int capturedPiece = record.capturedPiece;
        castlingRights = record.castlingRights;
        enPassantTarget = record.enPassantTarget;
        halfTimeClock = record.halfTimeClock;
        colorBB[0] = record.colorBB[0];
        colorBB[1] = record.colorBB[1];
        hash = record.hash;

        int moveFrom = move & 0x3F;
        int moveTo = (move >> 6) & 0x3F;
        int moveSpecial = (move >> 12) & 0xF;

        int piece = board[moveTo];
        if (moveSpecial >= 2 && moveSpecial <= 7) {
            piece = PIECE_PAWN + sideToMove;
        }

        board[moveFrom] = piece;
        board[moveTo] = capturedPiece;

        uint64_t fromBB = 1ULL << moveFrom;
        uint64_t toBB = 1ULL << moveTo;
        uint64_t moveMask = fromBB | toBB;
        int pieceType = piece % 8;

        if (moveSpecial == 0) {
            pieceBB[us][pieceType] ^= moveMask;
            if (pieceType == PIECE_KING) {
                if (us == 0) darkKing = moveFrom;
                else lightKing = moveFrom;
            }
        } else if (moveSpecial == 1) { // En Passant
            pieceBB[us][PIECE_PAWN] ^= moveMask;
            int epSquare = (us == 0) ? moveTo + 8 : moveTo - 8;
            board[epSquare] = (us == 0) ? PIECE_PAWN + 8 : PIECE_PAWN;
            pieceBB[them][PIECE_PAWN] ^= (1ULL << epSquare);
        } else if (moveSpecial == 11) { // Castle Queen Side
            pieceBB[us][PIECE_KING] ^= moveMask;
            if (us == 0) {
                board[56] = PIECE_ROOK; board[59] = 0;
                darkKing = 60;
                pieceBB[us][PIECE_ROOK] ^= (1ULL << 56) | (1ULL << 59);
            } else {
                board[0] = PIECE_ROOK + 8; board[3] = 0;
                lightKing = 4;
                pieceBB[us][PIECE_ROOK] ^= (1ULL << 0) | (1ULL << 3);
            }
        } else if (moveSpecial == 12) { // Castle King Side
            pieceBB[us][PIECE_KING] ^= moveMask;
            if (us == 0) {
                board[63] = PIECE_ROOK; board[61] = 0;
                darkKing = 60;
                pieceBB[us][PIECE_ROOK] ^= (1ULL << 63) | (1ULL << 61);
            } else {
                board[7] = PIECE_ROOK + 8; board[5] = 0;
                lightKing = 4;
                pieceBB[us][PIECE_ROOK] ^= (1ULL << 7) | (1ULL << 5);
            }
        } else { // Promotion
            pieceBB[us][moveSpecial] ^= toBB;
            pieceBB[us][PIECE_PAWN] ^= fromBB;
        }

        if (capturedPiece > 0 && moveSpecial != 1) {
            pieceBB[them][capturedPiece % 8] ^= toBB;
        }
    }

    
    uint64_t Engine::perft(int depth, int ply) {
        if (depth == 0) return 1;

        // int64_t lookup = TTlookupPerft(depth);
        // if (lookup >= 0) {
        //     return lookup;
        // }

        auto [numMoves, incheck] = getLegalMoves(false, ply, false);

        if (depth == 1) return numMoves;

        uint64_t total = 0;

        for (int i = 0; i < numMoves; i++) {
            makeMove(moveBuffer[ply][i]);
            total += perft(depth - 1, ply + 1);
            unmakeMove();
        }

        // TTstorePerft(depth, static_cast<int64_t>(total));

        return total;
    }
    uint64_t Engine::go_perft(int depth) {
        if (depth == 0) {
            std::cout << "Nodes searched: 0" << std::endl;
            return 0;
        };

        auto [numMoves, incheck] = getLegalMoves(false, 0, false);

        uint64_t total = 0;

        for (int i = 0; i < numMoves; i++) {
            makeMove(moveBuffer[0][i]);
            uint64_t num = perft(depth - 1, 1);
            unmakeMove();

            std::cout << moveToUCI(moveBuffer[0][i]) << " : " << num << std::endl;
            total += num;
        }

        std::cout << "\nNodes searched: " << total << std::endl;

        return total;
    }
    
    void Engine::initializeTranspositionTable(int sizeBits) {
        TTSizeBits = sizeBits;
        TTSize = 1ULL << sizeBits;
        TTIndexMask = TTSize - 1;
        
        // Allocate the table
        TT.resize(TTSize);
        
        TTclear();
    }

    void Engine::TTclear() {
        for (size_t i = 0; i < TTSize; ++i) {
            for (int j = 0; j < 4; ++j) {
                TT[i].entries[j].key = ~0ULL; // ~0ULL is the 64-bit equivalent of -1
                TT[i].entries[j].eval = 0;
                TT[i].entries[j].matePly = 0;
                TT[i].entries[j].move = 0;
                TT[i].entries[j].depth = 0;
                TT[i].entries[j].nodeType = -5;
            }
        }
    }

    void Engine::TTstoreEvaluation(int64_t eval, int matePly, uint32_t move, int depth, int nodeType) {
        if (eval == 99999 || eval == -99999) {
            std::cout<<"ERROR" << std::endl;
        }
        TTBucket& bucket = TT[hash & TTIndexMask];
        int ind = 0;
        int lowestDepth = 128;

        for (int i = 0; i < 4; ++i) {
            if (bucket.entries[i].key == hash || bucket.entries[i].key == ~0ULL) {
                if (bucket.entries[i].depth > depth) {
                    // Since new depth is less, keep the older, better entry
                    return;
                }
                ind = i;
                break;
            }
            if (bucket.entries[i].depth < lowestDepth) {
                lowestDepth = bucket.entries[i].depth;
                ind = i;
            }
        }

        bucket.entries[ind].key = hash;
        bucket.entries[ind].eval = eval;
        bucket.entries[ind].matePly = matePly;
        bucket.entries[ind].move = move;
        bucket.entries[ind].depth = depth;
        bucket.entries[ind].nodeType = nodeType;
    }

    uint32_t Engine::TTgetMove() {
        if (TT.empty()) return 0; // Safety check in case it's called before initialization

        TTBucket& bucket = TT[hash & TTIndexMask];
        for (int i = 0; i < 4; ++i) {
            if (bucket.entries[i].key == hash) {
                return bucket.entries[i].move;
            }
        }
        return 0;
    }

    TTLookupResult Engine::TTlookupEval(int depth, int ply, int64_t alpha, int64_t beta) {
        TTBucket& bucket = TT[hash & TTIndexMask];

        for (int i = 0; i < 4; ++i) {
            if (bucket.entries[i].key == hash) {
                if (bucket.entries[i].depth < depth) {
                    // The stored entry is too shallow, failed
                    return {false, TT_LOOKUP_FAILED, 0, 0};
                }

                int8_t type = bucket.entries[i].nodeType;
                int64_t eval = bucket.entries[i].eval;
                int matePly = bucket.entries[i].matePly;
                uint32_t move = bucket.entries[i].move;

                if (type == TT_EXACT) {
                    return {true, eval, matePly, move};
                }
                if (type == TT_UPPER_BOUND && eval <= alpha) {
                    return {true, eval, matePly, move};
                }
                if (type == TT_LOWER_BOUND && eval >= beta) {
                    return {true, eval, matePly, move};
                }
            }
        }

        return {false, TT_LOOKUP_FAILED, 0, 0};
    }

    void Engine::TTstorePerft(int depth, int64_t nodes) {
        TTBucket& bucket = TT[hash & TTIndexMask];
        int ind = 0;
        int lowestDepth = 128;

        for (int i = 0; i < 4; ++i) {
            if (bucket.entries[i].key == hash && bucket.entries[i].depth == depth) {
                return; // We already have this entry, no point overwriting it
            }
            if (bucket.entries[i].key == ~0ULL) {
                ind = i;
                break;
            }
            if (bucket.entries[i].depth < lowestDepth) {
                lowestDepth = bucket.entries[i].depth;
                ind = i;
            }
        }

        bucket.entries[ind].key = hash;
        bucket.entries[ind].eval = nodes;
        bucket.entries[ind].depth = depth;
    }

    int64_t Engine::TTlookupPerft(int depth) {
        TTBucket& bucket = TT[hash & TTIndexMask];

        for (int i = 0; i < 4; ++i) {
            if (bucket.entries[i].key == hash && bucket.entries[i].depth == depth) {
                return bucket.entries[i].eval;
            }
        }

        return -1; // Represents 'nil' from Luau (since nodes are always >= 0)
    }

    std::pair<int, int> Engine::getGamePhase() {
        int whiteMaterial = 0;
        int blackMaterial = 0;

        for (int pos = 0; pos < 64; ++pos) {
            int piece = board[pos];
            if (piece == 0 || PieceValues[piece] <= 100 || piece == PIECE_KING || piece == PIECE_KING + 8) continue;

            if (piece > 8) {
                whiteMaterial += PieceValues[piece];
            } else {
                blackMaterial += PieceValues[piece];
            }
        }
        return {blackMaterial, whiteMaterial};
    }

    bool Engine::isInCheck(int side) {
        int us = (side == 8) ? 1 : 0;
        int them = 1 - us;
        int kingSq = (us == 1) ? lightKing : darkKing;
        uint64_t kingBB = 1ULL << kingSq;
        uint64_t allPieces = colorBB[0] | colorBB[1];

        // Pawns
        uint64_t enemyPawns = pieceBB[them][PIECE_PAWN];
        if (us == 1) {
            if (((enemyPawns >> 7) & ~FILE_A) & kingBB) return true;
            if (((enemyPawns >> 9) & ~FILE_H) & kingBB) return true;
        } else {
            if (((enemyPawns << 7) & ~FILE_H) & kingBB) return true;
            if (((enemyPawns << 9) & ~FILE_A) & kingBB) return true;
        }

        // Knights
        if (Moves::knightAttacks[kingSq] & pieceBB[them][PIECE_KNIGHT]) return true;

        // Kings
        if (Moves::kingAttacks[kingSq] & pieceBB[them][PIECE_KING]) return true;

        // Sliders
        if (Magics::getRookAttacks(kingSq, allPieces) & (pieceBB[them][PIECE_ROOK] | pieceBB[them][PIECE_QUEEN])) return true;
        if (Magics::getBishopAttacks(kingSq, allPieces) & (pieceBB[them][PIECE_BISHOP] | pieceBB[them][PIECE_QUEEN])) return true;

        return false;
    }

    std::pair<int, int> Engine::getLVAttacker(int sq, int side, uint64_t tradedPieces) {
        int us = (side == 8) ? 1 : 0;
        uint64_t ourPieces = colorBB[us] & ~tradedPieces;
        uint64_t allPieces = (colorBB[0] | colorBB[1]) & ~tradedPieces;

        // Pawns
        uint64_t pawns = pieceBB[us][PIECE_PAWN] & ourPieces;
        if (pawns) {
            uint64_t attackers = (us == 1) ? (Moves::darkPawnAttack[sq] & pawns) : (Moves::lightPawnAttack[sq] & pawns);
            if (attackers) {
                int pSq = __builtin_ctzll(attackers);
                return {pSq, (sq <= 7 || sq >= 56) ? PIECE_QUEEN : PIECE_PAWN};
            }
        }

        // Knights
        uint64_t knights = pieceBB[us][PIECE_KNIGHT] & ourPieces;
        if (knights) {
            uint64_t attackers = Moves::knightAttacks[sq] & knights;
            if (attackers) return {__builtin_ctzll(attackers), PIECE_KNIGHT};
        }

        // Bishops
        uint64_t bishops = pieceBB[us][PIECE_BISHOP] & ourPieces;
        if (bishops) {
            uint64_t attackers = Magics::getBishopAttacks(sq, allPieces) & bishops;
            if (attackers) return {__builtin_ctzll(attackers), PIECE_BISHOP};
        }

        // Rooks
        uint64_t rooks = pieceBB[us][PIECE_ROOK] & ourPieces;
        if (rooks) {
            uint64_t attackers = Magics::getRookAttacks(sq, allPieces) & rooks;
            if (attackers) return {__builtin_ctzll(attackers), PIECE_ROOK};
        }

        // Queens
        uint64_t queens = pieceBB[us][PIECE_QUEEN] & ourPieces;
        if (queens) {
            uint64_t attackers = (Magics::getRookAttacks(sq, allPieces) | Magics::getBishopAttacks(sq, allPieces)) & queens;
            if (attackers) return {__builtin_ctzll(attackers), PIECE_QUEEN};
        }

        // King
        uint64_t king = pieceBB[us][PIECE_KING] & ourPieces;
        if (king) {
            uint64_t attackers = Moves::kingAttacks[sq] & king;
            if (attackers) return {__builtin_ctzll(attackers), PIECE_KING};
        }

        return {-1, 0};
    }

    int Engine::SEE(int move) {
        int moveFrom = move & 0x3F;
        int targetSquare = (move >> 6) & 0x3F;
        int moveSpecial = (move >> 12) & 0xF;

        if (board[targetSquare] == 0) return 0;

        uint64_t tradedPieces = 1ULL << moveFrom;
        int currentSide = 8 - sideToMove;

        int gains[32];
        int depth = 0;
        gains[0] = PieceValues[board[targetSquare]];
        
        int lastAttacker = (moveSpecial < 2 || moveSpecial > 7) ? board[moveFrom] : moveSpecial;

        while (true) {
            if (depth >= 31) break;
            auto [pos, leastAttacker] = getLVAttacker(targetSquare, currentSide, tradedPieces);
            if (pos == -1) break;

            depth++;
            gains[depth] = PieceValues[lastAttacker];
            lastAttacker = leastAttacker;
            tradedPieces |= (1ULL << pos);

            currentSide = 8 - currentSide;
        }

        if (depth == 0) return gains[0];

        while (depth > 1) {
            depth--;
            int new_val = gains[depth] - gains[depth + 1];
            gains[depth] = (new_val < 0) ? 0 : new_val;
        }

        return gains[0] - gains[1];
    }

    SearchResult Engine::search(int depth, int alpha, int beta, double startTime, double timeLimit, int ply) {
        if (ply > cnt_maxd) cnt_maxd = ply;
        cnt_searchnodes++;

        if (ply >= 127) return {evaluate(), -1, 0};

        if (cnt_searchnodes % 2048 == 0) {
            double tick = getClock();
            // if (nextBreakTime > 0 && nextBreakTime <= tick) {
            //     // task.wait(0.1) equivalent logic would be handled by your main thread, 
            //     // but we update the timer here.
            //     nextBreakTime = tick + 2.0;
            // }
            if (tick - startTime >= timeLimit) {
                stopSearch = true;
                return {0, -1, 0};
            }
        }

        // Draw Detection (3-fold & 50-move rule)
        if (isDraw()) { //positionHistory[hash] >= 3 || halfTimeClock >= 100
            return {0, -1, 0};
        }

        // TT Lookup
        auto ttRes = TTlookupEval(depth, ply, alpha, beta);
        if (ttRes.success/* && positionHistory[hash] == 1*/) { // == 1 because the current pos is already in history
            cnt_trans++;
            int64_t ttEval = ttRes.eval;
            int ttMatePly = ttRes.matePly;
            
            if (ttEval > 10000) { ttEval -= ply; ttMatePly = ply; }
            if (ttEval < -10000) { ttEval += ply; ttMatePly = ply; }
            
            return {(int)ttEval, ttMatePly, (int)ttRes.move};
        }

        bool notInCheck = !isInCheck(sideToMove);
        int staticEval = 0;
        bool hasStaticEval = false;

        // Reverse Futility Pruning
        if (depth < NULL_MOVE_DEPTH && notInCheck && (beta < 9000 && beta > -9000)) {
            staticEval = evaluate();
            hasStaticEval = true;
            if (staticEval - (120 * depth) >= beta) {
                return {staticEval, -1, 0};
            }
        }

        // Null Move Pruning
        if (depth >= NULL_MOVE_DEPTH && notInCheck) {
            auto [bmat, wmat] = getGamePhase();
            if ((sideToMove == 8 && wmat > NULL_MOVE_MATCUTOFF && bmat > NULL_MOVE_MATCUTOFF / 2) || 
                (sideToMove == 0 && bmat > NULL_MOVE_MATCUTOFF && wmat > NULL_MOVE_MATCUTOFF / 2)) {
                
                sideToMove = 8 - sideToMove;
                int wasEnPassant = enPassantTarget;
                uint64_t oldHash = hash;

                if (wasEnPassant != 64) {
                    hash ^= enPassantZobristKeys[enPassantTarget] ^ enPassantZobristKeys[64] ^ sideToMoveZobristKey;
                    enPassantTarget = 64;
                } else {
                    hash ^= sideToMoveZobristKey;
                }
                totalMoves++;
                
                SearchResult nullRes = search(depth - 3, -beta, -beta + 1, startTime, timeLimit, ply + 1);
                int nullScore = -nullRes.score;

                if (stopSearch) {
                    if (ply == 0) return {0, -1, 0};
                    return {0, -1, 0};
                }

                totalMoves--;
                enPassantTarget = wasEnPassant;
                hash = oldHash;
                sideToMove = 8 - sideToMove;

                if (nullScore >= beta) {
                    cnt_nullmove++;
                    return {beta, -1, 0};
                }
            }
        }

        // Quiescence Search
        if (depth <= 0) {
            return quiescence(alpha, beta, startTime, timeLimit, ply);
        }

        // Move Generation
        MoveGenResult genRes = getLegalMoves(false, ply, false);
        int numMoves = genRes.totalMoves;
        bool inCheck = genRes.incheck;

        if (numMoves == 0) {
            if (inCheck) return {-20000 + ply, ply, 0};
            else return {0, -1, 0};
        }

        // Futility Pruning Setup
        bool futilityPruning = false;
        if (depth == 1 && !inCheck && (alpha < 9000 && alpha > -9000)) {
            if (!hasStaticEval) staticEval = evaluate();
            if (staticEval + 300 <= alpha) {
                futilityPruning = true;
            }
        }

        int matePly = -1;
        int evalType = TT_UPPER_BOUND;
        int bestMove = 0;

        int* moveScores = scoreBuffer[ply];
        uint32_t* moves = moveBuffer[ply];

        for (int i = 0; i < numMoves; ++i) {
            // Selection Sort
            int bestI = i;
            int bestScore = moveScores[i];
            for (int j = i + 1; j < numMoves; ++j) {
                if (moveScores[j] > bestScore) {
                    bestScore = moveScores[j];
                    bestI = j;
                }
            }
            std::swap(moveScores[i], moveScores[bestI]);
            std::swap(moves[i], moves[bestI]);

            int move = moves[i];
            int moveFrom = move & 0x3F;
            int targetSquare = (move >> 6) & 0x3F;
            int moveSpecial = (move >> 12) & 0xF;
            bool isNotCapture = (board[targetSquare] == 0);

            makeMove(move);

            // Futility Pruning Execution
            if (futilityPruning && (i > 4) && isNotCapture && (moveSpecial < 2 || moveSpecial > 7) && !isInCheck(sideToMove)) {
                unmakeMove();
                continue;
            }

            int extension = 0;
            bool givesCheck = isInCheck(sideToMove);

            int score, mate;
            if (i == 0) {
                SearchResult res = search(depth - 1 + extension, -beta, -alpha, startTime, timeLimit, ply + 1);
                score = -res.score;
                mate = res.matePly;
            } else {
                // Late Move Reduction
                int reduction = 0;
                if (depth >= 3 && i >= 3 && isNotCapture && (moveSpecial < 2 || moveSpecial > 7) && !inCheck && !givesCheck) {
                    double lnDepth = LMR_LOG[std::min(depth, 64)];
                    double lnMoveCount = LMR_LOG[std::min(i + 1, 64)];
                    double red = LMR_BASE + (lnDepth * lnMoveCount) / LMR_DIV;
                    
                    reduction = std::floor(red + 0.5);
                    if (reduction > depth - 1) reduction = depth - 1;
                    if (reduction < 0) reduction = 0;
                }

                SearchResult res = search(depth - reduction + extension - 1, -alpha - 1, -alpha, startTime, timeLimit, ply + 1);
                score = -res.score;
                mate = res.matePly;
                
                if (stopSearch) {
                    if (ply == 0) return {alpha, matePly, bestMove};
                    return {alpha, matePly, 0};
                }

                if (score > alpha) {
                    res = search(depth + extension - 1, -alpha - 1, -alpha, startTime, timeLimit, ply + 1);
                    score = -res.score;
                    mate = res.matePly;
                    
                    if (stopSearch) {
                        if (ply == 0) return {alpha, matePly, bestMove};
                        return {alpha, matePly, 0};
                    }

                    if (score > alpha && score < beta) {
                        res = search(depth + extension - 1, -beta, -alpha, startTime, timeLimit, ply + 1);
                        score = -res.score;
                        mate = res.matePly;
                    }
                }
            }
            unmakeMove();

            
            if (stopSearch) {
                if (ply == 0) return {alpha, matePly, bestMove};
                return {alpha, matePly, 0};
            }

            if (score >= beta) {
                if (isNotCapture && (moveSpecial < 2 || moveSpecial > 7)) {
                    // Exact Luau History Index: full piece * 64 + targetSquare
                    int historyIndx = board[moveFrom] * 64 + targetSquare;
                    int bonus = (depth * depth);
                    history[historyIndx] = history[historyIndx] - (history[historyIndx] / 16) + bonus;
                    if (history[historyIndx] > 4000) history[historyIndx] = 4000;

                    int km1 = killerMoves[2 * ply];
                    int km2 = killerMoves[2 * ply + 1];
                    if (move != km1 && move != km2) {
                        killerMoves[2 * ply + 1] = km1;
                        killerMoves[2 * ply] = move;
                    }
                }

                cnt_pruned++;

                int storeScore = score;
                if (storeScore > 10000) storeScore += ply;
                if (storeScore < -10000) storeScore -= ply;

                TTstoreEvaluation(storeScore, mate, move, depth, TT_LOWER_BOUND);
                return {score, matePly, 0};
            }


            if (score > alpha) {
                alpha = score;
                evalType = TT_EXACT;
                matePly = mate;
                bestMove = move;
            }
        }

        int storeScore = alpha;
        if (storeScore > 10000) storeScore += ply;
        if (storeScore < -10000) storeScore -= ply;

        TTstoreEvaluation(storeScore, matePly, bestMove, depth, evalType);

        if (ply == 0) return {alpha, matePly, bestMove};
        return {alpha, matePly, 0};
    }

    SearchResult Engine::quiescence(int alpha, int beta, double startTime, double timeLimit, int ply) {
        if (ply > cnt_maxd) cnt_maxd = ply;
        cnt_searchnodes++;

        if (ply >= 127) return {evaluate(), -1, 0};

        if ((cnt_searchnodes % 2048 == 0) && (getClock() - startTime >= timeLimit)) {
            stopSearch = true;
            return {0, -1, 0};
        }

        if (isDraw()) { //positionHistory[hash] >= 3 || halfTimeClock >= 100
            return {0, -1, 0};
        }

        int numMoves;
        bool inCheck = isInCheck(sideToMove);
        int standPat = 0;

        if (inCheck) {
            MoveGenResult genRes = getLegalMoves(false, ply, false);
            numMoves = genRes.totalMoves;
            if (numMoves == 0) return {-20000 + ply, ply, 0};
        } else {
            standPat = evaluate();
            if (standPat >= beta) return {beta, -1, 0};
            if (alpha < standPat) alpha = standPat;

            MoveGenResult genRes = getLegalMoves(false, ply, true);
            numMoves = genRes.totalMoves;
        }

        int matePly = -1;
        int* moveScores = scoreBuffer[ply];
        uint32_t* moves = moveBuffer[ply];

        for (int i = 0; i < numMoves; ++i) {
            int bestI = i;
            int bestScore = moveScores[i];
            for (int j = i + 1; j < numMoves; ++j) {
                if (moveScores[j] > bestScore) {
                    bestScore = moveScores[j];
                    bestI = j;
                }
            }
            std::swap(moveScores[i], moveScores[bestI]);
            std::swap(moves[i], moves[bestI]);

            int move = moves[i];

            // SEE Pruning
            if (!inCheck && moveScores[i] < 0) continue;

            // Delta Pruning
            int targetSquare = (move >> 6) & 0x3F;
            if (!inCheck && board[targetSquare] != 0 && standPat + PieceValues[board[targetSquare]] + 150 < alpha) {
                continue;
            }

            makeMove(move);
            SearchResult res = quiescence(-beta, -alpha, startTime, timeLimit, ply + 1);
            int score = -res.score;
            int mate = res.matePly;
            unmakeMove();

            if (stopSearch) return {alpha, matePly, 0};
            if (score >= beta) return {beta, matePly, 0};
            if (score > alpha) {
                alpha = score;
                matePly = mate;
            }
        }

        return {alpha, matePly, 0};
    }


    bool Engine::isDraw() {
        if (halfTimeClock >= 100) return true; // 50-move rule
        
        int count = 1;
        // We only need to check back to the last pawn move or capture
        int limit = totalMoves - halfTimeClock;
        if (limit < 0) limit = 0;
        
        // Step back by 2 to only check positions with the same side to move
        for (int i = totalMoves - 2; i >= limit; i -= 2) {
            if (moveLog[i].hash == hash) {
                count++;
                if (count >= 3) return true;
            }
        }
        return false;
    }
    


    int Engine::evaluate() {
        cnt_eval++;

        if (isDraw()) {
            return 0;
        }


        int gamePhase = 0;

        int whiteMaterial = 0;
        int blackMaterial = 0;

        int whiteKingSafety = 0;
        int blackKingSafety = 0;

        int whiteMobility = 0;
        int blackMobility = 0;

        int whiteEndGameEval = 0;
        int blackEndGameEval = 0;
        int whiteMiddleGameEval = 0;
        int blackMiddleGameEval = 0;

        int whitePawnStructure = 0;
        int blackPawnStructure = 0;
        int whitePassedPawnsBonus = 0;
        int blackPassedPawnsBonus = 0;

        int whitePawnsOnFile[8] = {0};
        int blackPawnsOnFile[8] = {0};

        int whiteKingPos = lightKing;
        int blackKingPos = darkKing;
        int whiteKingRank = whiteKingPos / 8, whiteKingFile = whiteKingPos % 8;
        int blackKingRank = blackKingPos / 8, blackKingFile = blackKingPos % 8;

        uint64_t notWhitePieces = ~colorBB[1];
        uint64_t notBlackPieces = ~colorBB[0];
        uint64_t allPieces = colorBB[0] | colorBB[1];

        uint64_t darkPawnAttacks = 0;
        uint64_t lightPawnAttacks = 0;

        bool whiteLightBishop = false, whiteDarkBishop = false;
        bool blackLightBishop = false, blackDarkBishop = false;

        
        for (int pos = 0; pos < 64; ++pos) {
            int piece = board[pos];
            if (piece == PIECE_PAWN) {
                int file = pos % 8;
                if (blackPawnsOnFile[file] > 0) {
                    blackPawnStructure -= EVAL_DOUBLEDPAWN_PENALTY;
                }
                blackPawnsOnFile[file] += 1;
                darkPawnAttacks |= Moves::darkPawnAttack[pos];
            } else if (piece == PIECE_PAWN + 8) {
                int file = pos % 8;
                if (whitePawnsOnFile[file] > 0) {
                    whitePawnStructure -= EVAL_DOUBLEDPAWN_PENALTY;
                }
                whitePawnsOnFile[file] += 1;
                lightPawnAttacks |= Moves::lightPawnAttack[pos];
            }
        }
        

        uint64_t notDarkPawnAttacks = ~darkPawnAttacks;
        uint64_t notLightPawnAttacks = ~lightPawnAttacks;

        for (int pos = 0; pos < 64; ++pos) {
            int piece = board[pos];
            if (piece == 0) continue;

            if (piece > 8) {
                whiteMaterial += PieceValues[piece];

                if (piece == PIECE_KING + 8) {
                    whiteMiddleGameEval += kingSquareBonus[pos];
                    whiteEndGameEval += kingSquareEndgameBonus[pos];
                } else if (piece == PIECE_PAWN + 8) {
                    whiteMiddleGameEval += pawnSquareBonus[pos];
                    whiteEndGameEval += pawnSquareEndgameBonus[pos];

                    /*
                    int file = pos % 8;
                    int rank = pos / 8;
                    int distanceF = file - whiteKingFile;
                    if (distanceF >= -1 && distanceF <= 1 && rank >= whiteKingRank) {
                        if (rank <= whiteKingRank + 2) {
                            whiteKingSafety += EVAL_PAWNSHIELD_BONUS;
                        } else {
                            whiteKingSafety -= EVAL_PAWNSHIELDRANK_PENALTY * (rank - whiteKingRank - 2);
                        }
                    }

                    uint64_t mask = 0;
                    if (file > 0) mask |= (0x0101010101010101ULL << (file - 1));
                    if (file < 7) mask |= (0x0101010101010101ULL << (file + 1));

                    if (!(mask & colorBB[1])) {
                        whitePawnStructure -= EVAL_ISOLATEDPAWN_PENALTY;
                    }

                    if (rank >= 3 && !((mask | (0x0101010101010101ULL << file)) & (~0ULL << (8 * rank)) & colorBB[0])) {
                        whitePassedPawnsBonus += EVAL_PASSEDPAWN_BONUS[rank + 1];

                        if (file > 0 && board[pos - 9] == PIECE_PAWN + 8) {
                            whitePassedPawnsBonus += EVAL_PROTECTEDPASSEDPAWN_BONUS;
                        } else if (file < 7 && board[pos - 7] == PIECE_PAWN + 8) {
                            whitePassedPawnsBonus += EVAL_PROTECTEDPASSEDPAWN_BONUS;
                        }
                    }

                    if (blackPawnsOnFile[file] == 0 && !((mask & (~0ULL << (8 * rank)) & colorBB[1]) || (mask & ((rank >= 3) ? ~0ULL : ~(~0ULL << (8 * (rank + 1)))) & colorBB[1]))) {
                        if (rank >= 3 && (darkPawnAttacks & (1ULL << (pos + 8)))) {
                            whitePawnStructure -= EVAL_BACKWARDSPAWN_PENALTY;
                            if (!(colorBB[1] & (0x0101010101010101ULL << file) & (~0ULL << (8 * (rank + 1))))) {
                                whitePawnStructure -= EVAL_BACKWARDSPAWNOPEN_PENALTY;
                            }
                        } else if (rank < 3 && (darkPawnAttacks & (1ULL << (pos + 8)))) {
                            whitePawnStructure -= EVAL_BACKWARDSPAWN_PENALTY;
                            if (!((colorBB[1] & (0x0101010101010101ULL << file) & (~0ULL << (8 * (rank + 1)))))) {
                                whitePawnStructure -= EVAL_BACKWARDSPAWNOPEN_PENALTY;
                            }
                        }
                    }
                    */
                } else {
                    int bonus = piecePositionBonuses[piece - 8][pos];
                    whiteMiddleGameEval += bonus;
                    whiteEndGameEval += bonus;
                    gamePhase += gamePhaseValues[piece - 8];

                    if (piece == PIECE_KNIGHT + 8) {
                        whiteMobility += EVAL_MOBILITY_WEIGHTS[2] * (__builtin_popcountll(Moves::knightAttacks[pos] & notWhitePieces & notDarkPawnAttacks) - EVAL_EXPECTED_MOBILITY[2]);
                    } else if (piece == PIECE_ROOK + 8) {
                        whiteMobility += EVAL_MOBILITY_WEIGHTS[5] * (__builtin_popcountll(Magics::getRookAttacks(pos, allPieces) & notWhitePieces & notDarkPawnAttacks) - EVAL_EXPECTED_MOBILITY[5]);
                    } else if (piece == PIECE_BISHOP + 8) {
                        whiteMobility += EVAL_MOBILITY_WEIGHTS[6] * (__builtin_popcountll(Magics::getBishopAttacks(pos, allPieces) & notWhitePieces & notDarkPawnAttacks) - EVAL_EXPECTED_MOBILITY[6]);
                        
                        /*
                        int file = pos % 8;
                        int rank = pos / 8;
                        if ((file + rank) % 2 == 0) whiteDarkBishop = true;
                        else whiteLightBishop = true;
                        */
                    } else if (piece == PIECE_QUEEN + 8) {
                        whiteMobility += EVAL_MOBILITY_WEIGHTS[7] * (__builtin_popcountll(Magics::getRookAttacks(pos, allPieces) & notWhitePieces & notDarkPawnAttacks));
                        whiteMobility += EVAL_MOBILITY_WEIGHTS[7] * (__builtin_popcountll(Magics::getBishopAttacks(pos, allPieces) & notWhitePieces & notDarkPawnAttacks));
                        whiteMobility -= EVAL_EXPECTED_MOBILITY[7] * EVAL_MOBILITY_WEIGHTS[7];
                    }
                }
            } else {
                blackMaterial += PieceValues[piece];
                int flippedPos = pos ^ 56;

                if (piece == PIECE_KING) {
                    blackMiddleGameEval += kingSquareBonus[flippedPos];
                    blackEndGameEval += kingSquareEndgameBonus[flippedPos];
                } else if (piece == PIECE_PAWN) {
                    blackMiddleGameEval += pawnSquareBonus[flippedPos];
                    blackEndGameEval += pawnSquareEndgameBonus[flippedPos];

                    /*
                    int file = pos % 8;
                    int rank = pos / 8;
                    int distanceF = file - blackKingFile;
                    if (distanceF >= -1 && distanceF <= 1 && rank <= blackKingRank) {
                        if (rank >= blackKingRank - 2) {
                            blackKingSafety += EVAL_PAWNSHIELD_BONUS;
                        } else {
                            blackKingSafety -= EVAL_PAWNSHIELDRANK_PENALTY * (blackKingRank - rank - 2);
                        }
                    }

                    uint64_t mask = 0;
                    if (file > 0) mask |= (0x0101010101010101ULL << (file - 1));
                    if (file < 7) mask |= (0x0101010101010101ULL << (file + 1));

                    if (!(mask & colorBB[0])) {
                        blackPawnStructure -= EVAL_ISOLATEDPAWN_PENALTY;
                    }

                    if (rank <= 4 && !((mask | (0x0101010101010101ULL << file)) & ~(~0ULL << (8 * rank)) & colorBB[1])) {
                        blackPassedPawnsBonus += EVAL_PASSEDPAWN_BONUS[8 - rank];

                        if (file > 0 && board[pos + 7] == PIECE_PAWN) {
                            blackPassedPawnsBonus += EVAL_PROTECTEDPASSEDPAWN_BONUS;
                        } else if (file < 7 && board[pos + 9] == PIECE_PAWN) {
                            blackPassedPawnsBonus += EVAL_PROTECTEDPASSEDPAWN_BONUS;
                        }
                    }

                    if (whitePawnsOnFile[file] == 0 && !((mask & ((rank <= 4) ? ~0ULL : ~(~0ULL << (8 * rank))) & colorBB[0]) || (mask & ~(~0ULL << (8 * rank)) & colorBB[0]))) {
                        if (rank >= 5 && (lightPawnAttacks & (1ULL << (pos - 8)))) {
                            blackPawnStructure -= EVAL_BACKWARDSPAWN_PENALTY;
                            if (!(colorBB[0] & (0x0101010101010101ULL << file) & ~(~0ULL << (8 * rank)))) {
                                blackPawnStructure -= EVAL_BACKWARDSPAWNOPEN_PENALTY;
                            }
                        } else if (rank < 5 && rank > 0 && (lightPawnAttacks & (1ULL << (pos - 8)))) {
                            blackPawnStructure -= EVAL_BACKWARDSPAWN_PENALTY;
                            if (!(colorBB[0] & (0x0101010101010101ULL << file) & ~(~0ULL << (8 * rank)))) {
                                blackPawnStructure -= EVAL_BACKWARDSPAWNOPEN_PENALTY;
                            }
                        }
                    }
                    */
                } else {
                    int bonus = piecePositionBonuses[piece][flippedPos];
                    blackMiddleGameEval += bonus;
                    blackEndGameEval += bonus;
                    gamePhase += gamePhaseValues[piece];

                    if (piece == PIECE_KNIGHT) {
                        blackMobility += EVAL_MOBILITY_WEIGHTS[2] * (__builtin_popcountll(Moves::knightAttacks[pos] & notBlackPieces & notLightPawnAttacks) - EVAL_EXPECTED_MOBILITY[2]);
                    } else if (piece == PIECE_ROOK) {
                        blackMobility += EVAL_MOBILITY_WEIGHTS[5] * (__builtin_popcountll(Magics::getRookAttacks(pos, allPieces) & notBlackPieces & notLightPawnAttacks) - EVAL_EXPECTED_MOBILITY[5]);
                    } else if (piece == PIECE_BISHOP) {
                        blackMobility += EVAL_MOBILITY_WEIGHTS[6] * (__builtin_popcountll(Magics::getBishopAttacks(pos, allPieces) & notBlackPieces & notLightPawnAttacks) - EVAL_EXPECTED_MOBILITY[6]);
                        
                        /*
                        int file = pos % 8;
                        int rank = pos / 8;
                        if ((file + rank) % 2 == 0) blackDarkBishop = true;
                        else blackLightBishop = true;
                        */
                    } else if (piece == PIECE_QUEEN) {
                        blackMobility += EVAL_MOBILITY_WEIGHTS[7] * (__builtin_popcountll(Magics::getRookAttacks(pos, allPieces) & notBlackPieces & notLightPawnAttacks));
                        blackMobility += EVAL_MOBILITY_WEIGHTS[7] * (__builtin_popcountll(Magics::getBishopAttacks(pos, allPieces) & notBlackPieces & notLightPawnAttacks));
                        blackMobility -= EVAL_EXPECTED_MOBILITY[7] * EVAL_MOBILITY_WEIGHTS[7];
                    }
                }
            }
        }

        /*
        if (whiteLightBishop && whiteDarkBishop) whiteMobility += EVAL_BISHOPPAIR_BONUS;
        if (blackLightBishop && blackDarkBishop) blackMobility += EVAL_BISHOPPAIR_BONUS;

        // White King Safety
        if (whiteKingFile > 0) {
            if (blackPawnsOnFile[whiteKingFile] == 0) whiteKingSafety -= EVAL_EMPTYENEMYFILE_PENALTY;
            if (whitePawnsOnFile[whiteKingFile] == 0) whiteKingSafety -= EVAL_EMPTYFRIENDLYFILE_PENALTY;
        }
        if (whiteKingFile < 7) {
            if (blackPawnsOnFile[whiteKingFile + 2] == 0) whiteKingSafety -= EVAL_EMPTYENEMYFILE_PENALTY;
            if (whitePawnsOnFile[whiteKingFile + 2] == 0) whiteKingSafety -= EVAL_EMPTYFRIENDLYFILE_PENALTY;
        }
        if (blackPawnsOnFile[whiteKingFile + 1] == 0) {
            whiteKingSafety -= EVAL_EMPTYENEMYFILE_PENALTY + EVAL_OPENKINGFILE_PENALTY;
            if (whitePawnsOnFile[whiteKingFile + 1] == 0) {
                whiteKingSafety -= EVAL_EMPTYFRIENDLYFILE_PENALTY + EVAL_OPENKINGFILE_PENALTY;
            }
        } else if (whitePawnsOnFile[whiteKingFile + 1] == 0) {
            whiteKingSafety -= EVAL_EMPTYFRIENDLYFILE_PENALTY + EVAL_OPENKINGFILE_PENALTY;
        }

        // Black King Safety
        if (blackKingFile > 0) {
            if (whitePawnsOnFile[blackKingFile] == 0) blackKingSafety -= EVAL_EMPTYENEMYFILE_PENALTY;
            if (blackPawnsOnFile[blackKingFile] == 0) blackKingSafety -= EVAL_EMPTYFRIENDLYFILE_PENALTY;
        }
        if (blackKingFile < 7) {
            if (whitePawnsOnFile[blackKingFile + 2] == 0) blackKingSafety -= EVAL_EMPTYENEMYFILE_PENALTY;
            if (blackPawnsOnFile[blackKingFile + 2] == 0) blackKingSafety -= EVAL_EMPTYFRIENDLYFILE_PENALTY;
        }
        if (whitePawnsOnFile[blackKingFile + 1] == 0) blackKingSafety -= EVAL_EMPTYENEMYFILE_PENALTY;
        if (blackPawnsOnFile[blackKingFile + 1] == 0) blackKingSafety -= EVAL_EMPTYFRIENDLYFILE_PENALTY;

        whiteKingSafety += (castlingRights & 0b1100) ? EVAL_CASTLINGRIGHTS_BONUS : 0;
        blackKingSafety += (castlingRights & 0b0011) ? EVAL_CASTLINGRIGHTS_BONUS : 0;
        */

        whiteMiddleGameEval += whiteMaterial + whiteKingSafety + whiteMobility + whitePawnStructure + whitePassedPawnsBonus + ((sideToMove == 8) ? EVAL_TEMPO_BONUS : 0);
        blackMiddleGameEval += blackMaterial + blackKingSafety + blackMobility + blackPawnStructure + blackPassedPawnsBonus + ((sideToMove == 0) ? EVAL_TEMPO_BONUS : 0);

        whiteEndGameEval += whiteMaterial + whiteMobility + whitePawnStructure + whitePassedPawnsBonus * 2 + ((sideToMove == 8) ? EVAL_ENDGAMETEMPO_BONUS : 0);
        blackEndGameEval += blackMaterial + blackMobility + blackPawnStructure + blackPassedPawnsBonus * 2 + ((sideToMove == 0) ? EVAL_ENDGAMETEMPO_BONUS : 0);

        if (gamePhase <= 8) {
            if (whiteMaterial > blackMaterial + 200) {
                int multi = (whiteMaterial - blackMaterial - 50) / 150;
                if (multi > 4) multi = 4;
                whiteEndGameEval += endGameKingEval[darkKing + lightKing * 64];
            } else if (blackMaterial > whiteMaterial + 200) {
                int multi = (blackMaterial - whiteMaterial - 50) / 150;
                if (multi > 4) multi = 4;
                blackEndGameEval += endGameKingEval[lightKing + darkKing * 64];
            }
        }

        if (gamePhase > 24) gamePhase = 24;
        int endGamePhase = 24 - gamePhase;

        int whiteEval = (whiteMiddleGameEval * gamePhase + whiteEndGameEval * endGamePhase) / 24;
        int blackEval = (blackMiddleGameEval * gamePhase + blackEndGameEval * endGamePhase) / 24;

        if (sideToMove == 0) {
            return blackEval - whiteEval;
        }
        return whiteEval - blackEval;
    }

    void Engine::reset() {
        TTclear();

        // Clear History Heuristic
        for (int i = 0; i < 16 * 64; ++i) {
            history[i] = 0;
        }

        // Clear Killer Moves
        for (int i = 0; i < 256; ++i) {
            killerMoves[i] = 0;
        }

        resetCounters();
    }

    void Engine::resetCounters() {
        cnt_trans = 0;
        cnt_eval = 0;
        cnt_pruned = 0;
        cnt_maxd = -1;
        cnt_nullmove = 0;
        cnt_searchnodes = 0;
        stopSearch = false;
    }

    IDResult Engine::go_iterativeDeepening(double timeLimitMs) {
        resetCounters();
        stopSearch = false;

        MoveGenResult genRes = getLegalMoves(false, 0, false);
        if (genRes.totalMoves == 1) {
            int forcedMove = moveBuffer[0][0];
            std::cout << "info depth 1 nodes 0 pv " << moveToUCI(forcedMove) << std::endl;
            std::cout << "bestmove " << moveToUCI(forcedMove) << std::endl;
            return {forcedMove, 0, 1};
        }
        
        double startTime = getClock();
        timeLimitMs -= 3;
        double timeLimit = timeLimitMs / 1000.0; // Convert ms to seconds
        int depth = 1;
        int bestMove = 0;
        int bestScore = 0;

        int evalColorMultiplier = (sideToMove == 0) ? -1 : 1;

        nextBreakTime = startTime + 1.0;

        int alpha = -99999;
        int beta = 99999;


        while (getClock() - startTime < timeLimit) {
            if (depth > 100) break;


            // Aspiration window: "guess" the alpha and beta values, since they are likely to be similar to score
            /*
            if (depth >= 4) {
                alpha = bestScore - 100;
                beta = bestScore + 100;
            }
            */
            // position startpos moves d2d4 f7f5 c1g5 h7h6 g5f4 g8f6 e2e3 b8c6 g1f3 e7e6 f1e2 g7g5 f3e5 h8g8 e2h5 e8e7 e5f7 d8e8 f7h6 g8g6 h6f5 e6f5 f4g5 e7f7 g5f6 f7f6 d4d5 c6e5 e1g1 f8c5 b1c3 d7d6
            

            SearchResult res = search(depth, alpha, beta, startTime, timeLimit, 0);
            // std::cout << "search fin" << std::endl;
            if ((res.score <= alpha || res.score >= beta) && !stopSearch) {
                alpha = -99999;
                beta = 99999;
                res = search(depth, alpha, beta, startTime, timeLimit, 0);
            }
            
            depth++;
            
            
            
            if (stopSearch && depth > 1) {
                // std::cout << "*info depth " << (depth - 1) << " seldepth " << cnt_maxd << " score cp " << res.score * evalColorMultiplier << " nodes " << cnt_searchnodes << " pv " << moveToUCI(res.bestMove) << " | evals " << cnt_eval << " | prunes " << cnt_pruned << "; transpositions " << cnt_trans << "; null moves " << cnt_nullmove << std::endl;
                depth--;
                break;
            }

            
            bestMove = res.bestMove;
            bestScore = res.score;
            std::cout << "info depth " << (depth - 1) << " seldepth " << cnt_maxd << " score cp " << bestScore * evalColorMultiplier << " nodes " << cnt_searchnodes << " evals " << cnt_eval << " prunes " << cnt_pruned << " transpositions " << cnt_trans << " null moves " << cnt_nullmove << " pv " << moveToUCI(bestMove) << std::endl;
        }

        if (bestMove == 0) {
            getLegalMoves(false, 0, false);
            bestMove = moveBuffer[0][0];
        }

        std::cout << "bestmove " << moveToUCI(bestMove) << std::endl;
        std::cout << "info printed best move!" << std::endl;

        return {bestMove, bestScore * evalColorMultiplier, depth - 1};
    }
}
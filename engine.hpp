#include <string>
#include <vector>
#include <unordered_map>
#include <random>
#include <cctype>
#include <chrono>
#include <algorithm>

#ifndef ENGINE_HPP
#define ENGINE_HPP

#include <cstdint>
#include <utility>
#include <cmath>

namespace Chess {
    using uint8_t  = __UINT8_TYPE__;
    using uint16_t = __UINT16_TYPE__;
    using uint32_t = __UINT32_TYPE__;
    using uint64_t = __UINT64_TYPE__;
    using BB = uint64_t;
    using CastleRights = uint8_t;

    // --- Constants ---
    constexpr int PIECE_KING   = 1;
    constexpr int PIECE_KNIGHT = 2;
    constexpr int PIECE_PAWN   = 3;
    constexpr int PIECE_ROOK   = 5;
    constexpr int PIECE_BISHOP = 6;
    constexpr int PIECE_QUEEN  = 7;

    constexpr int COLOR_BLACK  = 0;
    constexpr int COLOR_WHITE  = 8;

    constexpr BB FILE_A = 0x0101010101010101ULL;
    constexpr BB FILE_B = 0x0101010101010101ULL<<1;
    constexpr BB FILE_C = 0x0101010101010101ULL<<2;
    constexpr BB FILE_D = 0x0101010101010101ULL<<3;
    constexpr BB FILE_E = 0x0101010101010101ULL<<4;
    constexpr BB FILE_F = 0x0101010101010101ULL<<5;
    constexpr BB FILE_G = 0x0101010101010101ULL<<6;
    constexpr BB FILE_H = 0x0101010101010101ULL<<7;

    // Move Scoring Constants (Adjust these to match your engine's actual values)
    constexpr int SCORE_HASHED      = 2'000'000;
    constexpr int SCORE_GOODCAPTURE = 1'000'000;
    constexpr int SCORE_BADCAPTURE  = -1'000'000;
    constexpr int SCORE_KILLER1     = 900'000;
    constexpr int SCORE_KILLER2     = 800'000;

    constexpr int NULL_MOVE_DEPTH = 3;
    constexpr int NULL_MOVE_MATCUTOFF = 400;
    constexpr double LMR_BASE = 0.77; // Replace with your exact Luau LMR_BASE
    constexpr double LMR_DIV = 2.85;  // Replace with your exact Luau LMR_DIV

    constexpr int8_t TT_LOWER_BOUND = -1;
    constexpr int8_t TT_EXACT = 0;
    constexpr int8_t TT_UPPER_BOUND = 1;
    constexpr int64_t TT_LOOKUP_FAILED = 2000000000000000000LL; // Equivalent to math.huge

    namespace Moves {
        extern const BB lightPawnAttack[];
        extern const BB darkPawnAttack[];
        extern const BB knightAttacks[];
        extern const BB kingAttacks[];
        extern const BB kingMoves[];
        extern const BB castlingRightsMask[];
        extern const CastleRights castlingRightsUpdater[];
        
        extern const uint64_t BISHOP_MAGICS[64];
        extern const uint64_t ROOK_MAGICS[64];
    }

    namespace Magics {
        extern uint64_t bishopAttacks[64][512];
        extern uint64_t rookAttacks[64][4096];
        extern uint64_t bishopMasks[64];
        extern uint64_t rookMasks[64];
        extern int bishopShifts[64];
        extern int rookShifts[64];
        extern uint64_t rayBetween[64][64];

        void init();

        inline uint64_t getBishopAttacks(int sq, uint64_t blockers) {
            blockers &= bishopMasks[sq];
            blockers *= Moves::BISHOP_MAGICS[sq];
            blockers >>= bishopShifts[sq];
            return bishopAttacks[sq][blockers];
        }

        inline uint64_t getRookAttacks(int sq, uint64_t blockers) {
            blockers &= rookMasks[sq];
            blockers *= Moves::ROOK_MAGICS[sq];
            blockers >>= rookShifts[sq];
            return rookAttacks[sq][blockers];
        }
    }

    struct SearchResult {
        int score;
        int matePly;
        int bestMove;
    };

    struct IDResult {
        int bestMove;
        int score;
        int depth;
    };

    struct TTEntry {
        uint64_t key;
        int64_t eval;       // int64_t to support large Perft node counts
        uint32_t move;
        int16_t matePly;
        int8_t depth;
        int8_t nodeType;
    };

    struct TTBucket {
        TTEntry entries[4];
    };

    struct TTLookupResult {
        bool success;
        int64_t eval;
        int matePly;
        uint32_t move;
    };

    struct MoveGenResult {
        int totalMoves;
        bool incheck;
    };

    struct UndoRecord {
        int move;
        uint8_t capturedPiece;
        uint8_t castlingRights;
        uint8_t enPassantTarget;
        int halfTimeClock;
        uint64_t colorBB[2];
        uint64_t hash;
    };

    class Engine {
    public:
        uint8_t board[64]; // Maintained for Evaluation & SEE
        int sideToMove;    // 0 for Black, 8 for White
        int lightKing;
        int darkKing;
        int enPassantTarget; // 0-63, or 64 if none
        int castlingRights;

        // Optimized Bitboard Arrays
        uint64_t pieceBB[2][8]; // [0=Black, 1=White][PieceType]
        uint64_t colorBB[2];    // [0=Black, 1=White]
        
        uint64_t hash;

        int killerMoves[256];
        int history[16 * 64];
        int piecePositionBonuses[8][64];
        int PieceValues[16];

        uint32_t moveBuffer[128][218];
        int scoreBuffer[128][218];

        UndoRecord moveLog[2048];
        int totalMoves;
        int halfTimeClock;
        std::unordered_map<uint64_t, int> positionHistory;
        
        // zobrist key variables
        uint64_t pieceZobristKeys[64][16];
        uint64_t castlingRightsZobristKeys[16];
        uint64_t enPassantZobristKeys[65];
        uint64_t sideToMoveZobristKey;

        // transposition table vars
        std::vector<TTBucket> TT;
        uint64_t TTSize;
        uint64_t TTIndexMask;
        int TTSizeBits;

        // search variables
        double nextBreakTime = 0.0;
        bool stopSearch = false;
        int cnt_maxd = 0;
        uint64_t cnt_searchnodes = 0;
        uint64_t cnt_trans = 0;
        uint64_t cnt_nullmove = 0;
        uint64_t cnt_pruned = 0;
        uint64_t cnt_eval = 0;

        // LMR Lookup Table
        static double LMR_LOG[65];
        static bool lmrInitialized;

        // Helper Methods
        std::pair<int, int> getGamePhase();
        bool isInCheck(int side);
        std::pair<int, int> getLVAttacker(int sq, int side, uint64_t tradedPieces);
        
        bool isDraw();
        int evaluate(); // Stub for your evaluation function

        // Search Methods
        SearchResult search(int depth, int alpha, int beta, double startTime, double timeLimit, int ply);
        SearchResult quiescence(int alpha, int beta, double startTime, double timeLimit, int ply);
        
        // Timer Helper
        static double getClock();


        void initializeTranspositionTable(int sizeBits);
        void TTclear();
        void TTstoreEvaluation(int64_t eval, int matePly, uint32_t move, int depth, int nodeType);
        TTLookupResult TTlookupEval(int depth, int ply, int64_t alpha, int64_t beta);
        
        void TTstorePerft(int depth, int64_t nodes);
        int64_t TTlookupPerft(int depth);
        uint32_t TTgetMove();


        void loadFEN(const std::string& fen);
        void initializeZobristKeys();
        void importZobristKeys(const std::string& json);
        void makeMove(int move);
        void unmakeMove();

        uint64_t perft(int depth, int ply);
        uint64_t go_perft(int depth);

        void reset();
        void resetCounters();
        IDResult go_iterativeDeepening(double timeLimitMs);

        

        inline int encodeMove(int from, int to, int special) {
            return special << 12 | to << 6 | from;
        }

        Engine();

        
        MoveGenResult getLegalMoves(bool noSort, int ply, bool onlyCaptures);

        // Stubs for your other functions
        int SEE(int move);


        static std::string squareToAlgebraic(int sq) {
            char file = 'a' + (sq % 8);
            char rank = '1' + (sq / 8);
            return {file, rank};
        }

        // Convert encoded move integer to standard UCI string
        static std::string moveToUCI(uint32_t move) {
            int from = move & 0x3F;
            int to = (move >> 6) & 0x3F;
            int special = (move >> 12) & 0x0F;

            std::string uci = squareToAlgebraic(from) + squareToAlgebraic(to);

            // Standard 4-bit move flags for promotions (adjust piece codes if needed)
            switch (special) {
                case 7:  uci += 'q'; break; // Queen promotion
                case 5:  uci += 'r'; break; // Rook promotion
                case 6: uci += 'b'; break; // Bishop promotion
                case 2: uci += 'n'; break; // Knight promotion
                default: break;
            }

            return uci;
        }
    };
}

#endif // ENGINE_HPP
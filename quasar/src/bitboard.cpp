#include "quasar/bitboard.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace quasar::bb {

std::array<Bitboard, 64> KnightAttacks{};
std::array<Bitboard, 64> KingAttacks{};
std::array<std::array<Bitboard, 64>, COLOR_NB> PawnAttacks{};
std::array<Bitboard, 64> RookMasks{};
std::array<Bitboard, 64> BishopMasks{};

namespace {

constexpr int ROOK_STRIDE = 4096;
constexpr int BISHOP_STRIDE = 512;

std::array<Bitboard, 64 * ROOK_STRIDE> RookTable{};
std::array<Bitboard, 64 * BISHOP_STRIDE> BishopTable{};
std::array<int, 64> RookShifts{};
std::array<int, 64> BishopShifts{};

// Generated once with a deterministic magic search, then embedded to keep
// engine startup instant. The attack tables are checked against slow ray
// generation in tests/test_main.cpp.
constexpr std::array<Bitboard, 64> ROOK_MAGICS = {
    0x0080021620804001ULL, 0x0040001000200041ULL, 0x0200102200088040ULL,
    0x4080040800821000ULL, 0x2200020004200810ULL, 0x4B00020C000D0008ULL,
    0x01000C4183000600ULL, 0x2080010000402C80ULL, 0x8002800826864000ULL,
    0x0410802000884000ULL, 0x0C01004010200100ULL, 0x020300100100203CULL,
    0x0450800801040080ULL, 0x4010800200040080ULL, 0x8804000208048110ULL,
    0x0C40800080004100ULL, 0xA2018880024004A0ULL, 0x0080848020004004ULL,
    0x1010410010200101ULL, 0x2010008008008010ULL, 0x0A08010004110008ULL,
    0x0802080104209040ULL, 0x0080040090010802ULL, 0x0280020000841069ULL,
    0x080C400080248000ULL, 0x2048850100224008ULL, 0x00200800C0300040ULL,
    0x11400D0100201000ULL, 0x0041001100080204ULL, 0x4802000200040810ULL,
    0x0100080C00103601ULL, 0x0020084200043085ULL, 0x0100804000800022ULL,
    0x0460401000402002ULL, 0x8309002001001044ULL, 0x0000800800801000ULL,
    0x0000800800800400ULL, 0xB542040080800200ULL, 0x1041000401000200ULL,
    0x000318B04A000401ULL, 0x0280082000484000ULL, 0x0080400081010030ULL,
    0x0010002000108080ULL, 0x012010002101000AULL, 0x0801000408010012ULL,
    0x0004008002008004ULL, 0x0AD1005200110014ULL, 0x4000004110820004ULL,
    0x9400400080003080ULL, 0x0000802200490200ULL, 0x1521100080200280ULL,
    0x9021000824100100ULL, 0x0081080080840280ULL, 0x0002000904100200ULL,
    0x0130024801302400ULL, 0x0102008100442200ULL, 0x0080984063800101ULL,
    0x0016810201412812ULL, 0x40200101603008C1ULL, 0x2851100004082101ULL,
    0x1049001002880005ULL, 0x0081000804000201ULL, 0x100020901208410CULL,
    0x0101064400813102ULL,
};

constexpr std::array<Bitboard, 64> BISHOP_MAGICS = {
    0x24E0440C00802202ULL, 0x00881808841A4500ULL, 0x29C1021085004190ULL,
    0x18C4041080042020ULL, 0x0841104000008108ULL, 0x890828080880C088ULL,
    0x0006021024062018ULL, 0x2000404044104040ULL, 0x09000504104A0210ULL,
    0x0088390204040820ULL, 0x4001420082008402ULL, 0x028108048B001142ULL,
    0x1C00140421001008ULL, 0x0008021212200400ULL, 0x080000581A082004ULL,
    0x3000048208027204ULL, 0x0120004044148482ULL, 0x4021000808108090ULL,
    0x0084011808009452ULL, 0x11C802242020E000ULL, 0x0124002210140002ULL,
    0x4009008200420200ULL, 0x0000830202100202ULL, 0x9002042500420200ULL,
    0x0A60200004480210ULL, 0x0402481020480080ULL, 0x8001100101004200ULL,
    0x6240104004004080ULL, 0x1124848014002000ULL, 0x00180200204100A0ULL,
    0x8020890844880800ULL, 0x0000802009040204ULL, 0x0410042041100280ULL,
    0x0804022000020440ULL, 0x2418280400480024ULL, 0x0801080800420A00ULL,
    0x4002248400020020ULL, 0x3020004102038084ULL, 0x84280110601C0200ULL,
    0x2004004208088080ULL, 0x0008022220041210ULL, 0x00820E0120000440ULL,
    0x0002002201020822ULL, 0x0000002019000804ULL, 0x0211204C10101100ULL,
    0x0604808081001200ULL, 0x1010029204030041ULL, 0x1008090102110621ULL,
    0x0002015002100C00ULL, 0x06002C040404400AULL, 0xC030002201100011ULL,
    0x4040008020884000ULL, 0x0248000903040100ULL, 0xC010092008008040ULL,
    0x6008084108020494ULL, 0x28102182008E0042ULL, 0x4100440A44022000ULL,
    0x4080020111491002ULL, 0x0108100084008800ULL, 0x0022242100420221ULL,
    0x10A8008110020210ULL, 0x400019122A900102ULL, 0x00800A1051080300ULL,
    0x0420222088008080ULL,
};

Bitboard slidingAttack(int sq, Bitboard occupancy, int dr, int df) {
    Bitboard attacks = 0;
    int r = rankOf(sq) + dr;
    int f = fileOf(sq) + df;
    while (r >= 0 && r < 8 && f >= 0 && f < 8) {
        const Bitboard target = bit(makeSquare(f, r));
        attacks |= target;
        if (occupancy & target) {
            break;
        }
        r += dr;
        f += df;
    }
    return attacks;
}

Bitboard makeSlidingMask(int sq, int dr, int df) {
    Bitboard mask = 0;
    int r = rankOf(sq) + dr;
    int f = fileOf(sq) + df;
    // Stop before the edge square: edge squares never block a ray.
    while (r + dr >= 0 && r + dr < 8 && f + df >= 0 && f + df < 8) {
        mask |= bit(makeSquare(f, r));
        r += dr;
        f += df;
    }
    return mask;
}

Bitboard makeRookMask(int sq) {
    return makeSlidingMask(sq, 1, 0) | makeSlidingMask(sq, -1, 0) |
           makeSlidingMask(sq, 0, 1) | makeSlidingMask(sq, 0, -1);
}

Bitboard makeBishopMask(int sq) {
    return makeSlidingMask(sq, 1, 1) | makeSlidingMask(sq, 1, -1) |
           makeSlidingMask(sq, -1, 1) | makeSlidingMask(sq, -1, -1);
}

void initSliding(bool rook, std::array<Bitboard, 64>& masks,
                 const std::array<Bitboard, 64>& magics, std::array<int, 64>& shifts,
                 Bitboard* table, int stride) {
    std::array<Bitboard, 4096> occupancies{};
    std::array<Bitboard, 4096> references{};

    for (int sq = 0; sq < 64; ++sq) {
        const Bitboard mask = rook ? makeRookMask(sq) : makeBishopMask(sq);
        masks[sq] = mask;
        const int bits = std::popcount(mask);
        const int count = 1 << bits;
        const Bitboard magic = magics[sq];
        shifts[sq] = 64 - bits;

        int n = 0;
        Bitboard subset = 0;
        do {
            occupancies[n] = subset;
            references[n] = rook
                ? (slidingAttack(sq, subset, 1, 0) | slidingAttack(sq, subset, -1, 0) |
                   slidingAttack(sq, subset, 0, 1) | slidingAttack(sq, subset, 0, -1))
                : (slidingAttack(sq, subset, 1, 1) | slidingAttack(sq, subset, 1, -1) |
                   slidingAttack(sq, subset, -1, 1) | slidingAttack(sq, subset, -1, -1));
            ++n;
            subset = (subset - mask) & mask;
        } while (subset != 0);

        for (int i = 0; i < count; ++i) {
            const int index =
                static_cast<int>(((occupancies[i] & mask) * magic) >> shifts[sq]);
            table[sq * stride + index] = references[i];
        }
    }
}

void initLeapers() {
    constexpr int knightDr[8] = {1, 2, 2, 1, -1, -2, -2, -1};
    constexpr int knightDf[8] = {2, 1, -1, -2, -2, -1, 1, 2};

    for (int sq = 0; sq < 64; ++sq) {
        const int r = rankOf(sq);
        const int f = fileOf(sq);

        for (int i = 0; i < 8; ++i) {
            const int nr = r + knightDr[i];
            const int nf = f + knightDf[i];
            if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                KnightAttacks[sq] |= bit(makeSquare(nf, nr));
            }
        }

        for (int dr = -1; dr <= 1; ++dr) {
            for (int df = -1; df <= 1; ++df) {
                if (dr == 0 && df == 0) {
                    continue;
                }
                const int nr = r + dr;
                const int nf = f + df;
                if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                    KingAttacks[sq] |= bit(makeSquare(nf, nr));
                }
            }
        }

        if (r < 7) {
            if (f > 0) {
                PawnAttacks[WHITE][sq] |= bit(makeSquare(f - 1, r + 1));
            }
            if (f < 7) {
                PawnAttacks[WHITE][sq] |= bit(makeSquare(f + 1, r + 1));
            }
        }
        if (r > 0) {
            if (f > 0) {
                PawnAttacks[BLACK][sq] |= bit(makeSquare(f - 1, r - 1));
            }
            if (f < 7) {
                PawnAttacks[BLACK][sq] |= bit(makeSquare(f + 1, r - 1));
            }
        }
    }
}

}  // namespace

void init() {
    static bool initialised = false;
    if (initialised) {
        return;
    }
    initialised = true;

    initLeapers();
    initSliding(true, RookMasks, ROOK_MAGICS, RookShifts, RookTable.data(), ROOK_STRIDE);
    initSliding(false, BishopMasks, BISHOP_MAGICS, BishopShifts, BishopTable.data(),
                BISHOP_STRIDE);
}

Bitboard rookAttacks(int sq, Bitboard occupancy) {
    const Bitboard occ = occupancy & RookMasks[sq];
    const int index = static_cast<int>((occ * ROOK_MAGICS[sq]) >> RookShifts[sq]);
    return RookTable[static_cast<std::size_t>(sq) * ROOK_STRIDE + index];
}

Bitboard bishopAttacks(int sq, Bitboard occupancy) {
    const Bitboard occ = occupancy & BishopMasks[sq];
    const int index = static_cast<int>((occ * BISHOP_MAGICS[sq]) >> BishopShifts[sq]);
    return BishopTable[static_cast<std::size_t>(sq) * BISHOP_STRIDE + index];
}

Bitboard queenAttacks(int sq, Bitboard occupancy) {
    return rookAttacks(sq, occupancy) | bishopAttacks(sq, occupancy);
}

Bitboard attacks(PieceType pt, int sq, Bitboard occupancy) {
    switch (pt) {
        case KNIGHT: return KnightAttacks[sq];
        case BISHOP: return bishopAttacks(sq, occupancy);
        case ROOK: return rookAttacks(sq, occupancy);
        case QUEEN: return queenAttacks(sq, occupancy);
        case KING: return KingAttacks[sq];
        default: return 0;
    }
}

}  // namespace quasar::bb

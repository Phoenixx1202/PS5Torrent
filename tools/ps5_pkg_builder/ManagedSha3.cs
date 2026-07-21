using System;
using System.Buffers.Binary;
using System.IO;

namespace LibProsperoPkg.Util;

/// <summary>
/// Portable SHA3-256 used when macOS does not expose SHA-3 through .NET's
/// platform cryptography provider.
/// </summary>
public static class ManagedSha3
{
    private const int Rate = 136;

    private static readonly ulong[] RoundConstants =
    [
        0x0000000000000001UL, 0x0000000000008082UL,
        0x800000000000808aUL, 0x8000000080008000UL,
        0x000000000000808bUL, 0x0000000080000001UL,
        0x8000000080008081UL, 0x8000000000008009UL,
        0x000000000000008aUL, 0x0000000000000088UL,
        0x0000000080008009UL, 0x000000008000000aUL,
        0x000000008000808bUL, 0x800000000000008bUL,
        0x8000000000008089UL, 0x8000000000008003UL,
        0x8000000000008002UL, 0x8000000000000080UL,
        0x000000000000800aUL, 0x800000008000000aUL,
        0x8000000080008081UL, 0x8000000000008080UL,
        0x0000000080000001UL, 0x8000000080008008UL,
    ];

    private static readonly int[] RotationOffsets =
    [
         0,  1, 62, 28, 27,
        36, 44,  6, 55, 20,
         3, 10, 43, 25, 39,
        41, 45, 15, 21,  8,
        18,  2, 61, 56, 14,
    ];

    public static byte[] HashData(ReadOnlySpan<byte> data)
    {
        var state = new ulong[25];

        while (data.Length >= Rate)
        {
            AbsorbBlock(state, data[..Rate]);
            data = data[Rate..];
        }

        Span<byte> finalBlock = stackalloc byte[Rate];
        finalBlock.Clear();
        data.CopyTo(finalBlock);
        finalBlock[data.Length] ^= 0x06;
        finalBlock[Rate - 1] ^= 0x80;
        AbsorbBlock(state, finalBlock);

        var digest = new byte[32];
        for (var lane = 0; lane < digest.Length / 8; lane++)
            BinaryPrimitives.WriteUInt64LittleEndian(digest.AsSpan(lane * 8), state[lane]);
        return digest;
    }

    public static byte[] HashData(Stream input)
    {
        ArgumentNullException.ThrowIfNull(input);
        using var copy = new MemoryStream();
        input.CopyTo(copy);
        return HashData(copy.GetBuffer().AsSpan(0, checked((int)copy.Length)));
    }

    public static int HashData(ReadOnlySpan<byte> data, Span<byte> destination)
    {
        if (destination.Length < 32)
            throw new ArgumentException("Destination must hold 32 bytes.", nameof(destination));
        HashData(data).CopyTo(destination);
        return 32;
    }

    private static void AbsorbBlock(ulong[] state, ReadOnlySpan<byte> block)
    {
        for (var lane = 0; lane < Rate / 8; lane++)
            state[lane] ^= BinaryPrimitives.ReadUInt64LittleEndian(block.Slice(lane * 8, 8));
        Permute(state);
    }

    private static void Permute(ulong[] state)
    {
        var c = new ulong[5];
        var d = new ulong[5];
        var b = new ulong[25];

        foreach (var roundConstant in RoundConstants)
        {
            for (var x = 0; x < 5; x++)
                c[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^
                       state[x + 15] ^ state[x + 20];
            for (var x = 0; x < 5; x++)
                d[x] = c[(x + 4) % 5] ^ RotateLeft(c[(x + 1) % 5], 1);
            for (var y = 0; y < 5; y++)
                for (var x = 0; x < 5; x++)
                    state[x + 5 * y] ^= d[x];

            for (var y = 0; y < 5; y++)
                for (var x = 0; x < 5; x++)
                {
                    var targetX = y;
                    var targetY = (2 * x + 3 * y) % 5;
                    b[targetX + 5 * targetY] =
                        RotateLeft(state[x + 5 * y], RotationOffsets[x + 5 * y]);
                }

            for (var y = 0; y < 5; y++)
                for (var x = 0; x < 5; x++)
                    state[x + 5 * y] = b[x + 5 * y] ^
                        ((~b[(x + 1) % 5 + 5 * y]) & b[(x + 2) % 5 + 5 * y]);

            state[0] ^= roundConstant;
        }
    }

    private static ulong RotateLeft(ulong value, int count) =>
        count == 0 ? value : (value << count) | (value >> (64 - count));
}

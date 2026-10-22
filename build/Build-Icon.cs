using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

internal static class BuildIcon
{
    private static int Main(string[] args)
    {
        if (args.Length != 2) return 2;
        using (var source = new Bitmap(args[0]))
        using (var cropped = MakeTransparentAndCrop(source))
        using (var output = new MemoryStream())
        using (var writer = new BinaryWriter(output))
        {
            int[] sizes = { 16, 24, 32, 48, 64 };
            var frames = new List<byte[]>();
            foreach (int size in sizes) frames.Add(MakeFrame(cropped, size));

            writer.Write((ushort)0); writer.Write((ushort)1); writer.Write((ushort)sizes.Length);
            int offset = 6 + 16 * sizes.Length;
            for (int i = 0; i < sizes.Length; i++)
            {
                int size = sizes[i]; byte[] frame = frames[i];
                writer.Write((byte)(size == 256 ? 0 : size));
                writer.Write((byte)(size == 256 ? 0 : size));
                writer.Write((byte)0); writer.Write((byte)0);
                writer.Write((ushort)1); writer.Write((ushort)32);
                writer.Write((uint)frame.Length); writer.Write((uint)offset);
                offset += frame.Length;
            }
            foreach (byte[] frame in frames) writer.Write(frame);
            File.WriteAllBytes(args[1], output.ToArray());
        }
        return 0;
    }

    private static Bitmap MakeTransparentAndCrop(Bitmap input)
    {
        int w = input.Width, h = input.Height;
        var seen = new bool[w, h];
        var queue = new Queue<Point>();
        Action<int, int> add = (x, y) =>
        {
            if (x >= 0 && y >= 0 && x < w && y < h && !seen[x, y] && IsBackground(input.GetPixel(x, y)))
            { seen[x, y] = true; queue.Enqueue(new Point(x, y)); }
        };
        for (int x = 0; x < w; x++) { add(x, 0); add(x, h - 1); }
        for (int y = 0; y < h; y++) { add(0, y); add(w - 1, y); }
        while (queue.Count > 0)
        {
            Point p = queue.Dequeue();
            add(p.X - 1, p.Y); add(p.X + 1, p.Y); add(p.X, p.Y - 1); add(p.X, p.Y + 1);
        }

        var transparent = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                transparent.SetPixel(x, y, seen[x, y] ? Color.Transparent : input.GetPixel(x, y));

        int left = w, top = h, right = -1, bottom = -1;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
            if (transparent.GetPixel(x, y).A > 0) { left = Math.Min(left, x); top = Math.Min(top, y); right = Math.Max(right, x); bottom = Math.Max(bottom, y); }
        if (right < left || bottom < top) throw new InvalidDataException("Icon source is blank.");
        int contentW = right - left + 1, contentH = bottom - top + 1;
        int side = Math.Max(contentW, contentH) + 2;
        var square = new Bitmap(side, side, PixelFormat.Format32bppArgb);
        using (Graphics g = Graphics.FromImage(square))
        {
            g.Clear(Color.Transparent);
            g.DrawImage(transparent, new Rectangle((side - contentW) / 2, (side - contentH) / 2, contentW, contentH),
                new Rectangle(left, top, contentW, contentH), GraphicsUnit.Pixel);
        }
        transparent.Dispose();
        return square;
    }

    private static bool IsBackground(Color c)
    {
        return c.A == 0 || (c.R >= 246 && c.G >= 246 && c.B >= 246 && Math.Max(c.R, Math.Max(c.G, c.B)) - Math.Min(c.R, Math.Min(c.G, c.B)) <= 8);
    }

    private static byte[] MakeFrame(Bitmap source, int size)
    {
        using (var bitmap = new Bitmap(size, size, PixelFormat.Format32bppArgb))
        using (Graphics g = Graphics.FromImage(bitmap))
        using (var stream = new MemoryStream())
        using (var writer = new BinaryWriter(stream))
        {
            g.Clear(Color.Transparent);
            g.CompositingQuality = CompositingQuality.HighQuality;
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.SmoothingMode = SmoothingMode.HighQuality;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.DrawImage(source, new Rectangle(0, 0, size, size));

            int xorSize = size * size * 4;
            int maskStride = ((size + 31) / 32) * 4;
            int maskSize = maskStride * size;
            writer.Write((uint)40); writer.Write((int)size); writer.Write((int)(size * 2));
            writer.Write((ushort)1); writer.Write((ushort)32); writer.Write((uint)0);
            writer.Write((uint)xorSize); writer.Write(0); writer.Write(0); writer.Write((uint)0); writer.Write((uint)0);
            for (int y = size - 1; y >= 0; y--)
                for (int x = 0; x < size; x++)
                {
                    Color c = bitmap.GetPixel(x, y);
                    writer.Write(c.B); writer.Write(c.G); writer.Write(c.R); writer.Write(c.A);
                }
            writer.Write(new byte[maskSize]);
            return stream.ToArray();
        }
    }
}

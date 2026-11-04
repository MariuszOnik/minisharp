// Language v2: foreach, switch, List<T>, bitwise operators, char, string methods.
// The same file also compiles as real C# (tools: dotnet), so the expected output can be checked against .NET.
using System;
using System.Collections.Generic;

class Item {
    public string name;
    public int value;
    public Item(string n, int v) { name = n; value = v; }
}

class Program {
    static int Classify(int n) {
        switch (n) {
            case 0: return 100;
            case 1:
            case 2: return 200;
            default: return -1;
        }
    }

    static string Kind(char c) {
        switch (c) {
            case 'a': case 'e': case 'i': case 'o': case 'u': return "vowel";
            case ' ': return "space";
        }
        if (char.IsDigit(c)) return "digit";
        return "other";
    }

    static string Day(string s) {
        string r = "?";
        switch (s) {
            case "mon": r = "Monday"; break;
            case "tue": r = "Tuesday"; break;
            default: r = "unknown " + s; break;
        }
        return r;
    }

    static void Main() {
        // ---- List<T> ----
        List<int> xs = new List<int>();
        for (int i = 0; i < 10; i++) xs.Add(i * i);
        Console.WriteLine("count " + xs.Count + " xs[3]=" + xs[3]);
        xs[3] = -9;
        xs.RemoveAt(0);
        xs.Insert(0, 42);
        Console.WriteLine("first " + xs[0] + " " + xs[3] + " has81 " + xs.Contains(81) + " idx64 " + xs.IndexOf(64));
        Console.WriteLine("remove 81: " + xs.Remove(81) + " again: " + xs.Remove(81) + " count " + xs.Count);
        int sum = 0;
        foreach (int x in xs) sum += x;
        Console.WriteLine("sum " + sum);

        var names = new List<string> { "ala", "ma", "kota" };
        names.Add("!");
        string joined = "";
        foreach (var n in names) joined += n + ".";
        Console.WriteLine(joined + " has 'ma': " + names.Contains("m" + "a"));
        names.Clear();
        Console.WriteLine("cleared " + names.Count);

        var items = new List<Item>();
        items.Add(new Item("sword", 10));
        items.Add(new Item("shield", 7));
        int total = 0;
        foreach (Item it in items) total += it.value;
        Console.WriteLine("items " + items.Count + " total " + total + " second " + items[1].name);

        List<List<int>> grid = new List<List<int>>();
        for (int y = 0; y < 3; y++) {
            grid.Add(new List<int>());
            for (int x = 0; x < 4; x++) grid[y].Add(y * 10 + x);
        }
        Console.WriteLine("grid " + grid[2][3] + " rows " + grid.Count);

        // ---- foreach over arrays and strings, break/continue ----
        int[] arr = new int[] { 5, 3, 8, 1 };
        int best = 0;
        foreach (var v in arr) { if (v == 3) continue; if (v == 1) break; if (v > best) best = v; }
        Console.WriteLine("best " + best);
        int letters = 0;
        foreach (char ch in "a1 b2 c3") if (char.IsLetter(ch)) letters++;
        Console.WriteLine("letters " + letters);

        // ---- switch ----
        Console.WriteLine(Classify(0) + " " + Classify(2) + " " + Classify(7));
        Console.WriteLine(Kind('e') + " " + Kind('7') + " " + Kind('x') + " " + Kind(' '));
        Console.WriteLine(Day("tue") + " / " + Day("sun"));
        int loops = 0;
        for (int i = 0; i < 6; i++) {
            switch (i % 3) {
                case 0: continue;
                case 1: loops += 10; break;
                default: loops += 1; break;
            }
            loops += 100;
        }
        Console.WriteLine("loops " + loops);

        // ---- bitwise ----
        int flags = 0;
        flags |= 1 << 3;
        flags |= 1;
        Console.WriteLine("flags " + flags + " and " + (flags & 8) + " xor " + (flags ^ 0xFF) + " not " + ~flags);
        flags &= ~1;
        flags <<= 2;
        Console.WriteLine("flags " + flags + " shr " + (flags >> 1) + " neg shr " + (-64 >> 3));
        flags >>= 4;
        flags ^= 3;
        Console.WriteLine("flags " + flags + " prec " + (1 + 2 << 3) + " " + (6 & 3 | 8));
        bool both = true & false, either = true | false, diff = true ^ true;
        Console.WriteLine(both + " " + either + " " + diff);

        // ---- char ----
        char c = 'A';
        c++;
        c += (char)2;
        int code = c;
        Console.WriteLine("char " + c + " code " + code + " next " + (char)(c + 1) + " plus " + (c + 1));
        char[] word = new char[3];
        word[0] = 'h'; word[1] = 'i'; word[2] = '!';
        string w = "";
        foreach (char k in word) w += k;
        Console.WriteLine(w + " " + char.ToUpper('q') + " " + (char.IsWhiteSpace(' ') ? "ws" : "no"));
        Console.WriteLine('x');

        // ---- strings ----
        string s = "  Hello, World  ";
        string t = s.Trim();
        Console.WriteLine("[" + t + "] len " + t.Length + " t[1]=" + t[1]);
        Console.WriteLine(t.ToUpper() + " " + t.ToLower() + " " + t.Substring(7) + " " + t.Substring(0, 5));
        Console.WriteLine(t.IndexOf("World") + " " + t.IndexOf('o') + " " + t.Contains("lo,") + " " + t.StartsWith("Hell") + " " + t.EndsWith("x"));
        Console.WriteLine(t.Replace("l", "L") + " " + "a-b-c".Replace("-", ""));
        string[] parts = "10,20,,30".Split(',');
        int psum = 0;
        foreach (string part in parts) if (!string.IsNullOrEmpty(part)) psum += int.Parse(part);
        Console.WriteLine("parts " + parts.Length + " sum " + psum + " float " + (float.Parse("2.5") * 2));
        int big = int.MaxValue;
        Console.WriteLine(big + " " + int.MinValue + " " + 12.ToString() + "!" + true.ToString());
    }
}

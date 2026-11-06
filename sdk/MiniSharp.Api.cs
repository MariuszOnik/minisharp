// MiniSharp API stubs - only for IntelliSense in VS Code.
// This file is never sent to the 3DS: the real implementation lives in src/natives.c.
// Keep it in sync with the native table there.
#pragma warning disable CS1591

// List<T>, IEnumerator and StartCoroutine work in MiniSharp without any 'using'; this makes VS Code agree.
global using System.Collections.Generic;
global using System.Collections;
global using static Coroutines;

/// <summary>Uchwyt korutyny (to, co zwraca StartCoroutine). W MiniSharp to to samo co IEnumerator.</summary>
public sealed class Coroutine { }

/// <summary>Korutyny jak w Unity. Wznawiane co klatkę po Update(), przed Draw().</summary>
public static class Coroutines {
    /// <summary>Uruchamia korutynę: wykonuje ją od razu do pierwszego yield, potem co klatkę.</summary>
    public static Coroutine StartCoroutine(IEnumerator routine) => null;
    /// <summary>Zatrzymuje korutynę.</summary>
    public static void StopCoroutine(Coroutine routine) { }
    /// <summary>Zatrzymuje korutynę.</summary>
    public static void StopCoroutine(IEnumerator routine) { }
    /// <summary>Zatrzymuje wszystkie korutyny.</summary>
    public static void StopAllCoroutines() { }
}

/// <summary>yield return new WaitForSeconds(s): czeka s sekund.</summary>
public sealed class WaitForSeconds { public WaitForSeconds(float seconds) { } }
/// <summary>To samo co WaitForSeconds (MiniSharp nie ma Time.timeScale).</summary>
public sealed class WaitForSecondsRealtime { public WaitForSecondsRealtime(float seconds) { } }
/// <summary>yield return new WaitForEndOfFrame(): to samo co yield return null (następna klatka).</summary>
public sealed class WaitForEndOfFrame { }

/// <summary>Wyjście tekstowe. Na 3DS widać je na dolnym ekranie w menu i w sdmc:/minisharp/log.txt.</summary>
public static class Console {
    /// <summary>Wypisuje wartość dowolnego typu i przechodzi do nowej linii.</summary>
    public static void WriteLine(object value) { }
    /// <summary>Wypisuje pustą linię.</summary>
    public static void WriteLine() { }
    /// <summary>Wypisuje wartość bez przejścia do nowej linii.</summary>
    public static void Write(object value) { }
}

/// <summary>Funkcje matematyczne. W MiniSharp wszystkie działają na float (double to alias float).</summary>
public static class Math {
    public const float PI = 3.14159265f;
    /// <summary>Sinus, kąt w radianach.</summary>
    public static float Sin(float a) => 0;
    /// <summary>Cosinus, kąt w radianach.</summary>
    public static float Cos(float a) => 0;
    /// <summary>Tangens, kąt w radianach.</summary>
    public static float Tan(float a) => 0;
    /// <summary>Kąt wektora (x, y) w radianach. Uwaga: najpierw y, potem x.</summary>
    public static float Atan2(float y, float x) => 0;
    public static float Sqrt(float v) => 0;
    public static float Pow(float b, float e) => 0;
    public static float Floor(float v) => 0;
    public static float Ceiling(float v) => 0;
    public static float Round(float v) => 0;
    public static int Abs(int v) => 0;
    public static float Abs(float v) => 0;
    public static int Min(int a, int b) => 0;
    public static float Min(float a, float b) => 0;
    public static int Max(int a, int b) => 0;
    public static float Max(float a, float b) => 0;
    /// <summary>Ogranicza v do przedziału [min, max].</summary>
    public static int Clamp(int v, int min, int max) => 0;
    /// <summary>Ogranicza v do przedziału [min, max].</summary>
    public static float Clamp(float v, float min, float max) => 0;
    public const float E = 2.71828183f;
    public static float Asin(float v) => 0;
    public static float Acos(float v) => 0;
    public static float Atan(float v) => 0;
    public static float Exp(float v) => 0;
    /// <summary>Logarytm naturalny.</summary>
    public static float Log(float v) => 0;
    public static float Log10(float v) => 0;
    /// <summary>Obcina część ułamkową (w stronę zera).</summary>
    public static float Truncate(float v) => 0;
    /// <summary>-1, 0 albo 1.</summary>
    public static int Sign(int v) => 0;
    /// <summary>-1, 0 albo 1.</summary>
    public static int Sign(float v) => 0;
}

/// <summary>Matematyka w stylu Unity (Mathf). Round zaokrągla połówki do parzystej, jak w Unity.</summary>
public static class Mathf {
    public const float PI = 3.14159265f;
    /// <summary>Mnożnik stopnie → radiany.</summary>
    public const float Deg2Rad = PI / 180f;
    /// <summary>Mnożnik radiany → stopnie.</summary>
    public const float Rad2Deg = 180f / PI;
    public const float Epsilon = 1.401298e-45f;
    public static float Sin(float a) => 0;
    public static float Cos(float a) => 0;
    public static float Tan(float a) => 0;
    public static float Asin(float v) => 0;
    public static float Acos(float v) => 0;
    public static float Atan(float v) => 0;
    public static float Atan2(float y, float x) => 0;
    public static float Sqrt(float v) => 0;
    public static float Pow(float b, float e) => 0;
    public static float Exp(float v) => 0;
    public static float Log(float v) => 0;
    public static float Log10(float v) => 0;
    public static float Floor(float v) => 0;
    public static float Ceil(float v) => 0;
    public static float Round(float v) => 0;
    public static int FloorToInt(float v) => 0;
    public static int CeilToInt(float v) => 0;
    public static int RoundToInt(float v) => 0;
    public static int Abs(int v) => 0;
    public static float Abs(float v) => 0;
    public static int Min(int a, int b) => 0;
    public static float Min(float a, float b) => 0;
    public static int Max(int a, int b) => 0;
    public static float Max(float a, float b) => 0;
    public static int Clamp(int v, int min, int max) => 0;
    public static float Clamp(float v, float min, float max) => 0;
    /// <summary>Ogranicza do 0..1.</summary>
    public static float Clamp01(float v) => 0;
    /// <summary>1 dla v ≥ 0, inaczej -1 (jak w Unity).</summary>
    public static float Sign(float v) => 0;
    /// <summary>a + (b - a) * t, t ograniczone do 0..1.</summary>
    public static float Lerp(float a, float b, float t) => 0;
    /// <summary>a + (b - a) * t bez ograniczania t.</summary>
    public static float LerpUnclamped(float a, float b, float t) => 0;
    /// <summary>Gdzie leży v między a i b (0..1).</summary>
    public static float InverseLerp(float a, float b, float v) => 0;
    /// <summary>Przesuwa current w stronę target o co najwyżej maxDelta.</summary>
    public static float MoveTowards(float current, float target, float maxDelta) => 0;
    /// <summary>Płynne przejście from → to (wolny start i koniec).</summary>
    public static float SmoothStep(float from, float to, float t) => 0;
    /// <summary>Zawija t do przedziału 0..length (jak modulo, także dla ujemnych).</summary>
    public static float Repeat(float t, float length) => 0;
    /// <summary>Odbija t tam i z powrotem w przedziale 0..length.</summary>
    public static float PingPong(float t, float length) => 0;
    /// <summary>Czy dwa floaty są prawie równe.</summary>
    public static bool Approximately(float a, float b) => false;
    /// <summary>Najkrótsza różnica kątów w stopniach (-180..180).</summary>
    public static float DeltaAngle(float current, float target) => 0;
}

/// <summary>Generator liczb losowych (jeden, globalny).</summary>
public static class Random {
    /// <summary>Losowa liczba od 0 do max-1. max musi być większe od 0.</summary>
    public static int Next(int max) => 0;
    /// <summary>Losowa liczba od min do max-1 (max nie wchodzi).</summary>
    public static int Range(int min, int max) => 0;
    /// <summary>Losowy float od 0 do 1.</summary>
    public static float Float() => 0;
    /// <summary>Ustawia ziarno, ten sam seed daje te same liczby.</summary>
    public static void Seed(int seed) { }
}

/// <summary>Rysowanie. Górny ekran 400x240, dolny 320x240, (0,0) to lewy górny róg.</summary>
public static class Gfx {
    /// <summary>Wybiera ekran do rysowania: Screen.Top albo Screen.Bottom.</summary>
    public static void Screen(int screen) { }
    /// <summary>Wypełnia cały bieżący ekran kolorem.</summary>
    public static void Clear(int color) { }
    /// <summary>Kolor tła: oba ekrany są nim czyszczone przed każdą klatką (jak kamera w Unity), więc w Draw nie trzeba Gfx.Clear. Domyślnie czarny; ustaw raz, np. w Main.</summary>
    public static void Background(int color) { }
    public static void Rect(float x, float y, float w, float h, int color) { }
    /// <summary>Wypełnione koło o środku (x, y) i promieniu r.</summary>
    public static void Circle(float x, float y, float r, int color) { }
    public static void Line(float x1, float y1, float x2, float y2, int color) { }
    /// <summary>Tekst w skali 0.5.</summary>
    public static void Text(float x, float y, string text, int color) { }
    /// <summary>Tekst w podanej skali (0.5 = domyślna).</summary>
    public static void Text(float x, float y, string text, float scale, int color) { }
    /// <summary>Składa kolor 0xRRGGBB z kanałów 0..255.</summary>
    public static int Rgb(int r, int g, int b) => 0;

    /// <summary>Wczytuje PNG/JPG/BMP (ścieżka względem folderu programu, max 1024x1024). Zwraca numer obrazka. Ten sam plik drugi raz daje ten sam numer.</summary>
    public static int LoadImage(string path) => 0;
    public static int ImageWidth(int image) => 0;
    public static int ImageHeight(int image) => 0;
    /// <summary>Rysuje obrazek; (x, y) to lewy górny róg.</summary>
    public static void Image(int image, float x, float y) { }
    /// <summary>Rysuje obrazek powiększony scale razy (np. 2 albo 3 dla pixel artu).</summary>
    public static void Image(int image, float x, float y, float scale) { }
    /// <summary>Skala osobno w poziomie i pionie (ujemna = odbicie lustrzane), obrót w stopniach wokół środka.</summary>
    public static void ImageEx(int image, float x, float y, float scaleX, float scaleY, float angle) { }
    /// <summary>Rysuje fragment obrazka (sx, sy, sw, sh), np. jedną klatkę ze sprite sheetu.</summary>
    public static void ImagePart(int image, float x, float y, int sx, int sy, int sw, int sh) { }
    /// <summary>Fragment obrazka ze skalą (ujemna = odbicie) i obrotem w stopniach.</summary>
    public static void ImagePartEx(int image, float x, float y, int sx, int sy, int sw, int sh, float scaleX, float scaleY, float angle) { }
}

/// <summary>Dźwięki z plików WAV (PCM 8 lub 16 bit, mono lub stereo). Do 24 dźwięków naraz.</summary>
public static class Audio {
    /// <summary>Wczytuje WAV (ścieżka względem folderu programu). Zwraca numer dźwięku.</summary>
    public static int Load(string path) => 0;
    public static void Play(int sound) { }
    /// <summary>Głośność 0..1.</summary>
    public static void Play(int sound, float volume) { }
    /// <summary>Odtwarza w pętli (muzyka).</summary>
    public static void PlayLoop(int sound) { }
    public static void PlayLoop(int sound, float volume) { }
    /// <summary>Zatrzymuje ten dźwięk.</summary>
    public static void Stop(int sound) { }
    public static void StopAll() { }
}

/// <summary>Przyciski, ekran dotykowy i circle pad.</summary>
public static class Input {
    /// <summary>true, dopóki przycisk jest wciśnięty.</summary>
    public static bool Held(int button) => false;
    /// <summary>true tylko w klatce, w której przycisk został wciśnięty.</summary>
    public static bool Pressed(int button) => false;
    /// <summary>true, gdy rysik dotyka dolnego ekranu.</summary>
    public static bool Touching() => false;
    public static int TouchX() => 0;
    public static int TouchY() => 0;
    /// <summary>Circle pad w poziomie, -1..1.</summary>
    public static float StickX() => 0;
    /// <summary>Circle pad w pionie, -1..1 (w górę dodatnie).</summary>
    public static float StickY() => 0;
}

/// <summary>Przyciski dla Input.Held / Input.Pressed. START jest zarezerwowany (powrót do menu).</summary>
public static class Btn {
    public const int A = 1 << 0, B = 1 << 1, X = 1 << 2, Y = 1 << 3;
    public const int L = 1 << 4, R = 1 << 5, ZL = 1 << 6, ZR = 1 << 7;
    public const int Up = 1 << 8, Down = 1 << 9, Left = 1 << 10, Right = 1 << 11;
    public const int Select = 1 << 13;
}

/// <summary>Gotowe kolory 0xRRGGBB. Własne: Gfx.Rgb(r, g, b) albo literał 0xRRGGBB.</summary>
public static class Color {
    public const int Black = 0x000000, White = 0xFFFFFF, Red = 0xE53935, Green = 0x43A047;
    public const int Blue = 0x1E88E5, Yellow = 0xFDD835, Cyan = 0x00ACC1, Magenta = 0xD81B60;
    public const int Orange = 0xFB8C00, Gray = 0x808080, DarkGray = 0x303030;
}

public static class Screen {
    public const int Top = 0, Bottom = 1;
    public const int TopWidth = 400, BottomWidth = 320, Height = 240;
}

public static class Time {
    /// <summary>Milisekundy od startu programu.</summary>
    public static int Millis() => 0;
    /// <summary>Zmierzone klatki na sekundę (na PC zawsze 60).</summary>
    public static int Fps() => 0;
}

public static class App {
    /// <summary>Kończy program i wraca do menu.</summary>
    public static void Quit() { }
}

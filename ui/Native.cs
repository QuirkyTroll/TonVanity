using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace TonVanity.UI;

internal static class Native
{
    private const string Library = "tonvanity.dll";

    static Native()
    {
        NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, Resolve);
    }

    private static IntPtr Resolve(string name, Assembly assembly, DllImportSearchPath? path)
    {
        if (!string.Equals(name, Library, StringComparison.OrdinalIgnoreCase))
        {
            return IntPtr.Zero;
        }
        string candidate = Path.Combine(AppContext.BaseDirectory, Library);
        return File.Exists(candidate) ? NativeLibrary.Load(candidate) : IntPtr.Zero;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern IntPtr tnv_create(ref TnvConfig config);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int tnv_start(IntPtr handle);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern void tnv_stop(IntPtr handle);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern int tnv_poll(IntPtr handle, out TnvStats stats, [Out] TnvMatch[] matches, int maxMatches);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern void tnv_destroy(IntPtr handle);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern IntPtr tnv_backend_info();

    internal static string BackendInfo()
    {
        IntPtr ptr = tnv_backend_info();
        return ptr == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUTF8(ptr) ?? string.Empty;
    }

    internal static TnvConfig CreateConfig(int version, int scheme, int bounce, int mode, int caseSensitive, int cpuThreads, string suffix)
    {
        TnvConfig config = default;
        config.wallet_version = version;
        config.mnemonic_scheme = scheme;
        config.bounceable = bounce;
        config.compute_mode = mode;
        config.case_sensitive = caseSensitive;
        config.cpu_threads = cpuThreads;
        config.SetSuffix(suffix);
        return config;
    }

    internal static unsafe string Utf8(byte* value, int capacity)
    {
        int length = 0;
        while (length < capacity && value[length] != 0)
        {
            length++;
        }
        return Encoding.UTF8.GetString(value, length);
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct TnvConfig
{
    public int wallet_version;
    public int mnemonic_scheme;
    public int bounceable;
    public int compute_mode;
    public int case_sensitive;
    public int cpu_threads;
    public fixed byte suffix[64];

    internal void SetSuffix(string value)
    {
        fixed (byte* target = suffix)
        {
            for (int i = 0; i < 64; i++)
            {
                target[i] = 0;
            }
            byte[] bytes = Encoding.UTF8.GetBytes(value);
            int count = Math.Min(bytes.Length, 63);
            for (int i = 0; i < count; i++)
            {
                target[i] = bytes[i];
            }
        }
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct TnvStats
{
    public ulong attempts;
    public ulong candidates;
    public ulong found;
    public double elapsed_ms;
    public double attempts_per_sec;
    public double candidates_per_sec;
    public int backend;
    public int backend_state;
    public fixed byte backend_name[64];
    public fixed byte message[256];

    internal string BackendName()
    {
        fixed (byte* value = backend_name)
        {
            return Native.Utf8(value, 64);
        }
    }

    internal string Message()
    {
        fixed (byte* value = message)
        {
            return Native.Utf8(value, 256);
        }
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct TnvMatch
{
    public fixed byte address[64];
    public fixed byte mnemonic[512];
    public int wallet_version;
    public int mnemonic_scheme;
    public int bounceable;
    public int backend;
    public ulong attempts;

    internal string Address()
    {
        fixed (byte* value = address)
        {
            return Native.Utf8(value, 64);
        }
    }

    internal string Mnemonic()
    {
        fixed (byte* value = mnemonic)
        {
            return Native.Utf8(value, 512);
        }
    }
}

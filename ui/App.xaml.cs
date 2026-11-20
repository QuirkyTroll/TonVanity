using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;

namespace TonVanity.UI;

public partial class App : Application
{
    private Window? _window;

    public App()
    {
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _window = new MainWindow();
        _window.Activate();
    }
}

internal static class Bootstrap
{
    [StructLayout(LayoutKind.Sequential)]
    private struct PackageVersion
    {
        public ushort Major;
        public ushort Minor;
        public ushort Build;
        public ushort Revision;
    }

    [DllImport("Microsoft.WindowsAppRuntime.Bootstrap.dll", EntryPoint = "MddBootstrapInitialize2", CallingConvention = CallingConvention.StdCall)]
    private static extern int MddBootstrapInitialize2(uint majorMinorVersion, [MarshalAs(UnmanagedType.LPWStr)] string? versionTag, PackageVersion minVersion, uint options);

    [ModuleInitializer]
    internal static void Initialize()
    {
        try
        {
            MddBootstrapInitialize2(0x00010007, null, default, 0);
        }
        catch
        {
        }
    }
}

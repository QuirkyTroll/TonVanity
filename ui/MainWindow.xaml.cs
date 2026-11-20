using System.Collections.ObjectModel;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.Graphics;

namespace TonVanity.UI;

public sealed partial class MainWindow : Window
{
    private static readonly int[] VersionValues = { 1, 0, 2 };

    private readonly ObservableCollection<FoundItem> _found = new();
    private readonly TnvMatch[] _matches = new TnvMatch[32];
    private readonly DispatcherQueueTimer _timer;
    private ScrollViewer? _listScrollViewer;
    private IntPtr _handle;
    private bool _initialSizeApplied;
    private bool _busy;
    private bool _running;
    private bool _paused;
    private ulong _baseAttempts;
    private ulong _baseCandidates;
    private double _baseElapsedMs;

    public MainWindow()
    {
        InitializeComponent();
        Title = "TonVanity";
        string iconPath = System.IO.Path.Combine(AppContext.BaseDirectory, "app.ico");
        if (System.IO.File.Exists(iconPath))
        {
            AppWindow.SetIcon(iconPath);
        }
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);
        SystemBackdrop = new MicaBackdrop();
        if (AppWindowTitleBar.IsCustomizationSupported())
        {
            AppWindow.TitleBar.ButtonBackgroundColor = Colors.Transparent;
            AppWindow.TitleBar.ButtonInactiveBackgroundColor = Colors.Transparent;
        }
        RootGrid.Loaded += RootGrid_Loaded;
        RootGrid.AddHandler(UIElement.PointerWheelChangedEvent, new PointerEventHandler(RootGrid_PointerWheelChanged), true);
        LogList.Loaded += (_, _) => _listScrollViewer = FindDescendant<ScrollViewer>(LogList);
        LogList.ItemsSource = _found;
        _timer = DispatcherQueue.CreateTimer();
        _timer.Interval = TimeSpan.FromMilliseconds(250);
        _timer.Tick += Timer_Tick;
        Closed += OnClosed;
        SetIdleControls();
    }

    private void RootGrid_Loaded(object sender, RoutedEventArgs e)
    {
        if (_initialSizeApplied)
        {
            return;
        }
        _initialSizeApplied = true;
        double scale = RootGrid.XamlRoot?.RasterizationScale ?? 1.0;
        if (scale <= 0)
        {
            scale = 1.0;
        }
        AppWindow.Resize(new SizeInt32(
            (int)Math.Round(1000 * scale),
            (int)Math.Round(800 * scale)));
        if (AppWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.PreferredMinimumWidth = (int)Math.Round(900 * scale);
            presenter.PreferredMinimumHeight = (int)Math.Round(750 * scale);
        }
    }

    private void RootGrid_PointerWheelChanged(object sender, PointerRoutedEventArgs e)
    {
        if (e.OriginalSource is DependencyObject source && IsWithin(source, LogList))
        {
            return;
        }
        _listScrollViewer ??= FindDescendant<ScrollViewer>(LogList);
        if (_listScrollViewer is null)
        {
            return;
        }
        int delta = e.GetCurrentPoint(RootGrid).Properties.MouseWheelDelta;
        if (delta == 0)
        {
            return;
        }
        _listScrollViewer.ChangeView(null, _listScrollViewer.VerticalOffset - delta, null, true);
        e.Handled = true;
    }

    private static T? FindDescendant<T>(DependencyObject root) where T : DependencyObject
    {
        int count = VisualTreeHelper.GetChildrenCount(root);
        for (int i = 0; i < count; i++)
        {
            DependencyObject child = VisualTreeHelper.GetChild(root, i);
            if (child is T match)
            {
                return match;
            }
            T? nested = FindDescendant<T>(child);
            if (nested is not null)
            {
                return nested;
            }
        }
        return null;
    }

    private static bool IsWithin(DependencyObject? node, DependencyObject root)
    {
        while (node is not null)
        {
            if (ReferenceEquals(node, root))
            {
                return true;
            }
            node = VisualTreeHelper.GetParent(node);
        }
        return false;
    }

    private async void StartButton_Click(object sender, RoutedEventArgs e)
    {
        if (_busy)
        {
            return;
        }
        if (_paused && _handle != IntPtr.Zero)
        {
            await ResumeAsync();
            return;
        }
        await StartAsync();
    }

    private async void PauseButton_Click(object sender, RoutedEventArgs e)
    {
        if (_busy || !_running || _handle == IntPtr.Zero)
        {
            return;
        }
        _busy = true;
        _timer.Stop();
        try
        {
            int count = Native.tnv_poll(_handle, out TnvStats stats, _matches, _matches.Length);
            _baseAttempts += stats.attempts;
            _baseCandidates += stats.candidates;
            _baseElapsedMs += stats.elapsed_ms;
            await Task.Run(() => Native.tnv_stop(_handle));
            _running = false;
            _paused = true;
            StateText.Text = "Пауза";
            StartButton.Content = "Продолжить";
            StartButton.IsEnabled = true;
            PauseButton.IsEnabled = false;
            StopButton.IsEnabled = true;
            StatusText.Text = "Поиск приостановлен";
        }
        catch (Exception ex)
        {
            StatusText.Text = ex.Message;
        }
        finally
        {
            _busy = false;
        }
    }

    private async void StopButton_Click(object sender, RoutedEventArgs e)
    {
        if (_busy || _handle == IntPtr.Zero)
        {
            return;
        }
        _busy = true;
        _timer.Stop();
        try
        {
            await Task.Run(() => Native.tnv_stop(_handle));
            Native.tnv_destroy(_handle);
            _handle = IntPtr.Zero;
            _running = false;
            _paused = false;
            _baseAttempts = 0;
            _baseCandidates = 0;
            _baseElapsedMs = 0;
            AttemptsText.Text = "0";
            SpeedText.Text = "0";
            CandidateSpeedText.Text = "0";
            ElapsedText.Text = "00:00:00";
            BackendText.Text = "Бэкенд: —";
            StateText.Text = "Остановлено";
            StatusText.Text = "Поиск остановлен";
            StartButton.Content = "Старт";
            SetIdleControls();
        }
        catch (Exception ex)
        {
            StatusText.Text = ex.Message;
        }
        finally
        {
            _busy = false;
        }
    }

    private async Task StartAsync()
    {
        string suffix = SuffixBox.Text.Trim();
        if (!ValidSuffix(suffix))
        {
            StatusText.Text = "Окончание: 1–48 символов A-Z, a-z, 0-9, - или _";
            return;
        }

        _busy = true;
        SetConfigEnabled(false);
        StartButton.IsEnabled = false;
        PauseButton.IsEnabled = false;
        StopButton.IsEnabled = true;
        StateText.Text = "Инициализация";
        StatusText.Text = "Инициализация бэкенда, первый запуск OpenCL может занять до 40 секунд";
        try
        {
            if (_handle == IntPtr.Zero)
            {
                int threads = double.IsNaN(ThreadsBox.Value) ? 0 : (int)ThreadsBox.Value;
                TnvConfig config = Native.CreateConfig(
                    VersionValues[VersionCombo.SelectedIndex],
                    WordsCombo.SelectedIndex,
                    FormatCombo.SelectedIndex,
                    ModeCombo.SelectedIndex,
                    CaseCheck.IsChecked == true ? 1 : 0,
                    threads,
                    suffix);
                _handle = Native.tnv_create(ref config);
                if (_handle == IntPtr.Zero)
                {
                    StatusText.Text = "Не удалось создать сессию: проверьте окончание и параметры";
                    SetIdleControls();
                    SetConfigEnabled(true);
                    return;
                }
                _baseAttempts = 0;
                _baseCandidates = 0;
                _baseElapsedMs = 0;
            }

            int result = await Task.Run(() => Native.tnv_start(_handle));
            if (result != 0)
            {
                StatusText.Text = "Не удалось запустить поиск";
                DestroyHandle();
                SetIdleControls();
                SetConfigEnabled(true);
                return;
            }

            _running = true;
            _paused = false;
            StateText.Text = "Работает";
            StatusText.Text = "Поиск запущен";
            StartButton.Content = "Старт";
            StartButton.IsEnabled = false;
            PauseButton.IsEnabled = true;
            StopButton.IsEnabled = true;
            _timer.Start();
        }
        catch (DllNotFoundException)
        {
            StatusText.Text = "tonvanity.dll не найден рядом с TonVanity.exe";
            DestroyHandle();
            SetIdleControls();
            SetConfigEnabled(true);
        }
        catch (BadImageFormatException)
        {
            StatusText.Text = "tonvanity.dll имеет неверную разрядность";
            DestroyHandle();
            SetIdleControls();
            SetConfigEnabled(true);
        }
        catch (Exception ex)
        {
            StatusText.Text = ex.Message;
            DestroyHandle();
            SetIdleControls();
            SetConfigEnabled(true);
        }
        finally
        {
            _busy = false;
        }
    }

    private async Task ResumeAsync()
    {
        _busy = true;
        StartButton.IsEnabled = false;
        PauseButton.IsEnabled = false;
        StopButton.IsEnabled = true;
        StateText.Text = "Инициализация";
        try
        {
            int result = await Task.Run(() => Native.tnv_start(_handle));
            if (result != 0)
            {
                StatusText.Text = "Не удалось возобновить поиск";
                StateText.Text = "Пауза";
                StartButton.IsEnabled = true;
                PauseButton.IsEnabled = false;
                return;
            }
            _running = true;
            _paused = false;
            StateText.Text = "Работает";
            StatusText.Text = "Поиск возобновлён";
            StartButton.IsEnabled = false;
            PauseButton.IsEnabled = true;
            _timer.Start();
        }
        catch (Exception ex)
        {
            StatusText.Text = ex.Message;
            StateText.Text = "Пауза";
            StartButton.IsEnabled = true;
        }
        finally
        {
            _busy = false;
        }
    }

    private void Timer_Tick(DispatcherQueueTimer sender, object args)
    {
        if (_handle == IntPtr.Zero)
        {
            return;
        }
        try
        {
            int count = Native.tnv_poll(_handle, out TnvStats stats, _matches, _matches.Length);
            for (int i = 0; i < count; i++)
            {
                TnvMatch match = _matches[i];
                FoundItem item = new()
                {
                    Address = match.Address(),
                    Mnemonic = match.Mnemonic(),
                    VersionLabel = VersionName(match.wallet_version),
                    FormatLabel = match.bounceable != 0 ? "EQ" : "UQ",
                    BackendLabel = BackendName(match.backend)
                };
                _found.Add(item);
                LogList.ScrollIntoView(item);
            }
            FoundCountText.Text = $"Найдено: {_found.Count}";
            AttemptsText.Text = (_baseAttempts + stats.attempts).ToString("N0");
            SpeedText.Text = stats.attempts_per_sec.ToString("N0");
            CandidateSpeedText.Text = stats.candidates_per_sec.ToString("N0");
            ElapsedText.Text = TimeSpan.FromMilliseconds(_baseElapsedMs + stats.elapsed_ms).ToString(@"hh\:mm\:ss");
            string backend = stats.BackendName();
            if (!string.IsNullOrWhiteSpace(backend))
            {
                BackendText.Text = $"Бэкенд: {backend}";
            }
            StateText.Text = stats.backend_state == 2 ? "Fallback" : _paused ? "Пауза" : "Работает";
            string message = stats.Message();
            if (!string.IsNullOrWhiteSpace(message))
            {
                StatusText.Text = message;
            }
        }
        catch (Exception ex)
        {
            _timer.Stop();
            StatusText.Text = ex.Message;
        }
    }

    private void OnClosed(object sender, WindowEventArgs args)
    {
        _timer.Stop();
        if (_handle != IntPtr.Zero)
        {
            try
            {
                Native.tnv_stop(_handle);
                Native.tnv_destroy(_handle);
            }
            catch
            {
            }
            _handle = IntPtr.Zero;
        }
    }

    private void DestroyHandle()
    {
        if (_handle == IntPtr.Zero)
        {
            return;
        }
        try
        {
            Native.tnv_destroy(_handle);
        }
        catch
        {
        }
        _handle = IntPtr.Zero;
    }

    private void SetIdleControls()
    {
        StartButton.IsEnabled = true;
        PauseButton.IsEnabled = false;
        StopButton.IsEnabled = false;
        StartButton.Content = "Старт";
        SetConfigEnabled(true);
    }

    private void SetConfigEnabled(bool enabled)
    {
        VersionCombo.IsEnabled = enabled;
        WordsCombo.IsEnabled = enabled;
        FormatCombo.IsEnabled = enabled;
        ModeCombo.IsEnabled = enabled;
        SuffixBox.IsEnabled = enabled;
        CaseCheck.IsEnabled = enabled;
        ThreadsBox.IsEnabled = enabled;
    }

    private static bool ValidSuffix(string value)
    {
        if (value.Length is < 1 or > 48)
        {
            return false;
        }
        foreach (char c in value)
        {
            bool ok = c is >= 'A' and <= 'Z' or >= 'a' and <= 'z' or >= '0' and <= '9' or '-' or '_';
            if (!ok)
            {
                return false;
            }
        }
        return true;
    }

    private static string VersionName(int version) => version switch
    {
        0 => "V4R2",
        1 => "W5",
        2 => "TGWallet",
        _ => "?"
    };

    private static string BackendName(int backend) => backend switch
    {
        1 => "CUDA",
        2 => "OpenCL",
        _ => "CPU"
    };
}

public sealed class FoundItem
{
    public string Address { get; set; } = string.Empty;
    public string Mnemonic { get; set; } = string.Empty;
    public string VersionLabel { get; set; } = string.Empty;
    public string FormatLabel { get; set; } = string.Empty;
    public string BackendLabel { get; set; } = string.Empty;
}

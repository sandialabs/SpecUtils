using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace SpecUtils;

internal static class NativeLibraryResolver
{
    private static bool _registered;

[System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2255")]
    [ModuleInitializer]
    internal static void Initialize()
    {
        if (_registered)
            return;
        _registered = true;

        NativeLibrary.SetDllImportResolver(
            typeof(NativeLibraryResolver).Assembly,
            ResolveDllImport);
    }

    private static IntPtr ResolveDllImport(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (libraryName != "libSpecUtils")
            return IntPtr.Zero;

        // Try SPECUTILS_NATIVE_LIB_DIR environment variable first
        string? envDir = Environment.GetEnvironmentVariable("SPECUTILS_NATIVE_LIB_DIR");
        if (!string.IsNullOrEmpty(envDir))
        {
            if (TryLoadFromDirectory(envDir, out IntPtr handle))
                return handle;
        }

        // Try the directory of the executing assembly
        string? assemblyDir = Path.GetDirectoryName(assembly.Location);
        if (!string.IsNullOrEmpty(assemblyDir))
        {
            if (TryLoadFromDirectory(assemblyDir, out IntPtr handle))
                return handle;
        }

        // Try current working directory
        if (TryLoadFromDirectory(Environment.CurrentDirectory, out IntPtr cwdHandle))
            return cwdHandle;

        // Fall back to default system search
        if (NativeLibrary.TryLoad(libraryName, assembly, searchPath, out IntPtr defaultHandle))
            return defaultHandle;

        return IntPtr.Zero;
    }

    private static bool TryLoadFromDirectory(string directory, out IntPtr handle)
    {
        handle = IntPtr.Zero;

        string[] candidateNames;
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            candidateNames = ["libSpecUtils.dll", "SpecUtils.dll"];
        else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            candidateNames = ["libSpecUtils.dylib"];
        else
            candidateNames = ["libSpecUtils.so"];

        foreach (string name in candidateNames)
        {
            string fullPath = Path.Combine(directory, name);
            if (NativeLibrary.TryLoad(fullPath, out handle))
                return true;
        }

        return false;
    }
}

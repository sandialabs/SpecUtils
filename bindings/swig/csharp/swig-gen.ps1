
$swig_exe="C:\Tools\swigwin-4.3.0-vanilla\swig.exe"
$outdir = "csharp_out"

# Check if $outdir exists
if (Test-Path $outdir) {
    # Remove all contents if it does
    Remove-Item "$outdir\*" -Recurse -Force
} else {
    # Create the directory if it does not exist
    New-Item -ItemType Directory -Path $outdir
}

& $swig_exe -I"..\\..\\..\\" -csharp -c++ -outdir $outdir -debug-classes -namespace "Sandia.SpecUtils" ./SpecUtilsCSharp.i
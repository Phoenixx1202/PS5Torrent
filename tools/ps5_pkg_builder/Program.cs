using LibProsperoPkg;
using LibProsperoPkg.Util;

if (args.Length != 2)
{
    Console.Error.WriteLine("Uso: PS5TorrentPkgBuilder PASTA_DO_APP PASTA_DE_SAIDA");
    return 2;
}

var source = Path.GetFullPath(args[0]);
var output = Path.GetFullPath(args[1]);
Directory.CreateDirectory(output);

var sha3Empty = Convert.ToHexString(ManagedSha3.HashData([])).ToLowerInvariant();
var sha3Abc = Convert.ToHexString(
    ManagedSha3.HashData(System.Text.Encoding.ASCII.GetBytes("abc"))).ToLowerInvariant();
if (sha3Empty != "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a" ||
    sha3Abc != "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532")
{
    Console.Error.WriteLine("Falha na autoverificação SHA3-256 do macOS.");
    return 1;
}

var options = new ProsperoHomebrewPackageOptions
{
    HomebrewFolder = source,
    OutputFolder = output,
    ModuleName = "eboot.bin",
    ContentId = "IV9999-PPSA00001_00-PS5TORRENT000001",
    Passcode = new string('0', 32),
    Title = "PS5Torrent",
    Version = "01.00",
};

try
{
    var result = ProsperoHomebrewPackager.Package(options, Console.WriteLine);
    Console.WriteLine($"PKG: {result.OutputPath}");
    Console.WriteLine($"Pronto para iniciar: {result.LaunchReadiness.IsLaunchReady}");
    foreach (var issue in result.LaunchReadiness.Issues)
        Console.WriteLine($"Problema: {issue}");
    foreach (var warning in result.Warnings)
        Console.WriteLine($"Aviso: {warning}");
    return result.LaunchReadiness.IsLaunchReady ? 0 : 1;
}
catch (Exception error)
{
    Console.Error.WriteLine($"Falha ao gerar o PKG: {error}");
    return 1;
}

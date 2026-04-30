import Development.Shake
import Development.Shake.FilePath
import Development.Shake.Util
import System.Directory (createDirectoryIfMissing)
import System.Environment (lookupEnv)
import Data.Maybe (fromMaybe)

buildDir :: FilePath
buildDir = "_build"

cxxFlags :: [String]
cxxFlags = ["-std=c++20", "-Iinc", "-I.", "-O2", "-g", "-Wall"]

cFlags :: [String]
cFlags = ["-std=c11", "-Iinc", "-I.", "-O2", "-g", "-Wall"]

main :: IO ()
main = shakeArgs shakeOptions { shakeFiles = buildDir } $ do
    want ["wasm-vm"]

    phony "clean" $ do
        putInfo "Cleaning build artifacts"
        removeFilesAfter buildDir ["//*"]
        removeFilesAfter "." ["wasm-vm"]
        removeFilesAfter "tests/wasm" ["//*"]

    phony "tests" $ do
        cTests   <- getDirectoryFiles "tests/ctests"   ["*.c"]
        watTests <- getDirectoryFiles "tests/wattests" ["*.wat"]
        let toWasm t = "tests/wasm" </> dropExtension t <.> "wasm"
        need (map toWasm cTests ++ map toWasm watTests)

    phony "test" $ do
        need ["wasm-vm", "tests"]
        cmd_ "./grade.sh"

    "wasm-vm" %> \out -> do
        cppSrcs <- getDirectoryFiles "" ["src//*.cpp", "main.cpp"]
        cSrcs   <- getDirectoryFiles "" ["src//*.c"]
        let objs = [ buildDir </> s <.> "o" | s <- cppSrcs ++ cSrcs ]
        need objs
        cmd_ "clang++" "-o" out objs "-lm"

    buildDir <//> "*.cpp.o" %> \out -> do
        let src = dropDirectory1 (dropExtension out)
        let dep = out -<.> "d"
        liftIO $ createDirectoryIfMissing True (takeDirectory out)
        () <- cmd "clang++" "-c" cxxFlags "-MMD" "-MF" dep "-o" out src
        needMakefileDependencies dep

    buildDir <//> "*.c.o" %> \out -> do
        let src = dropDirectory1 (dropExtension out)
        let dep = out -<.> "d"
        liftIO $ createDirectoryIfMissing True (takeDirectory out)
        () <- cmd "clang" "-c" cFlags "-MMD" "-MF" dep "-o" out src
        needMakefileDependencies dep

    "tests/wasm/*.wasm" %> \out -> do
        let name   = dropExtension (takeFileName out)
        let cSrc   = "tests/ctests"   </> name <.> "c"
        let watSrc = "tests/wattests" </> name <.> "wat"
        liftIO $ createDirectoryIfMissing True (takeDirectory out)
        cExists <- doesFileExist cSrc
        if cExists
          then do
            need [cSrc]
            wasmCc <- liftIO (fromMaybe "clang" <$> lookupEnv "WASM_CC")
            cmd_ wasmCc "--target=wasm32" "-O0" "-nostartfiles" "-nostdlib"
                 "-Wl,--no-entry" "-o" out cSrc
          else do
            need [watSrc]
            cmd_ "wat2wasm" watSrc "-o" out

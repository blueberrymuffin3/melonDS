{ stdenv
, lib
, fetchurl
, gnupg
, curl
, bash
, libarchive
, xz
, ncurses
, autoPatchelfHook
, llvmPackages
}:

stdenv.mkDerivation rec {
  pname = "devkitpro";
  version = "6.0.1";

  /*
  # Commands to generate these URLs:
docker run --rm -it archlinux
# Following commands in docker container:
pacman-key --init
pacman-key --recv BC26F752D25B92CE272E0F44F7FD5492264BB9D0 --keyserver keyserver.ubuntu.com
pacman-key --lsign BC26F752D25B92CE272E0F44F7FD5492264BB9D0
pacman -U https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.zst --noconfirm
pacman-key --populate devkitpro

cat >>/etc/pacman.conf <<EOF
[dkp-libs]
Server = https://pkg.devkitpro.org/packages
[dkp-linux]
Server = https://pkg.devkitpro.org/packages/linux/x86_64/
EOF

pacman -Sy --noconfirm
  */

  srcs = [
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/dkp-cmake-common-utils-1.5.2-1-any.pkg.tar.zst;
      sha256 = "0r2kmc421swik75p9hc00dpj4yay3ssy0hy7sggf1a9ziw94mjfx";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/catnip-0.1.0-1-any.pkg.tar.zst;
      sha256 = "0m7swl2pgp90l4i75sjxcydsy4yvsajzml1s1wv7a1mqq44b3yzy";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/libnx-4.8.0-1-any.pkg.tar.zst;
      sha256 = "110hk6a8ggz3m509g6arn35h0mhbs19q3rzj4vhisa5c6gqhbw2v";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/deko3d-0.5.0-1-any.pkg.tar.zst;
      sha256 = "1hwsgxp58fj72cg0l8fnfanich4x15w9nqd2q1cfdhj669bila6l";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/devkita64-cmake-1.1.2-1-any.pkg.tar.zst;
      sha256 = "18b8b2w9n6fph68kfai2pjsp1m7ycylyi3gwz3a13ly1wnjh9679";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/linux/x86_64/switch-tools-1.13.1-1-x86_64.pkg.tar.zst;
      sha256 = "1avxbia7xsw46c9jdmc4aqdkfg4ikq7696yclw2m4g2dfrrr82ny";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/linux/x86_64/uam-1.1.0-1-x86_64.pkg.tar.xz;
      sha256 = "0qpwfh9kwr2xi6bamc29pvwa0wy940djrswgicpwai8l0a16h6zg";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/switch-pkg-config-0.28-4-any.pkg.tar.xz;
      sha256 = "0bhb97sr36dahvv8vm61jpr5qszp9ph16jdl2811s5fd1ybj1fnc";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/switch-cmake-1.5.1-1-any.pkg.tar.zst;
      sha256 = "0pba51fsdyn8rck4kqvnfwyy13rgiqvqzx6vm5iaxq95x0k3sijq";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/switch-examples-20241018-1-any.pkg.tar.zst;
      sha256 = "09252bixjhd1v91fy3qfzy6v4gw9ja4vwmgdwh1w04jiv5b6lccd";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/linux/x86_64/devkit-env-1.0.1-2-any.pkg.tar.xz;
      sha256 = "16wxb27m99nrbiwywg0mvwfbikp5wybqfxafhkwjf4crl74852yg";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/linux/x86_64/general-tools-1.4.4-1-x86_64.pkg.tar.zst;
      sha256 = "1df7fx1lzjj3kmi1w0w3a3qb35ygyyfp93v6y2kq6krdsb9rpddn";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/devkita64-rules-1.1.1-1-any.pkg.tar.zst;
      sha256 = "1phyb9b3r07drqkyhc7wj82jl5gf5ymc5b3k003lfd144xwcyj96";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/linux/x86_64/devkitA64-r27-2-x86_64.pkg.tar.zst;
      sha256 = "0rcywycigg3nm19xiwh29zz40dflhzrysdbdq2zhnai9wm4dsdz9";
    })
    (fetchurl{
      url = https://pkg.devkitpro.org/packages/linux/x86_64/devkitA64-gdb-14.1-1-x86_64.pkg.tar.zst;
      sha256 = "1fig1b2nivpivyqn7mq3y4c958kvjn8dmb4hc98yhwq8z68rbhy8";
    })
  ];

  nativeBuildInputs = [
    autoPatchelfHook
    llvmPackages.bintools # For ar
  ];

  buildInputs = [
    # gpg, curl, bash, libarchive13, xz-utils, libc6, pkg-config, make, libarchive-tools
    gnupg
    curl
    bash
    libarchive
    xz
    ncurses
    stdenv.cc.cc.lib
  ];

  sourceRoot = ".";

  installPhase = ''
    runHook preInstall

    mkdir $out

    for src in $srcs;
    do
      tar -xf $src -C $out
    done

    runHook postInstall
  '';

  meta = with lib; {
    homepage = "https://studio-link.com";
    description = "Voip transfer";
    platforms = platforms.linux;
  };
}

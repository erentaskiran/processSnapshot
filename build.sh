#!/bin/bash

# Durum Kaydı & Geri Alma Sistemi - Build Script
# Arch Linux için

set -e  # Hata durumunda dur

echo "╔═══════════════════════════════════════════════════════════╗"
echo "║    Durum Kaydı & Geri Alma Sistemi - Build Script         ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

# Renk tanımları
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Fonksiyonlar
print_step() {
    echo -e "${GREEN}[STEP]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Gerekli araçları kontrol et
check_dependencies() {
    print_step "Bağımlılıklar kontrol ediliyor..."
    
    local missing=""
    
    if ! command -v cmake &> /dev/null; then
        missing="$missing cmake"
    fi
    
    if ! command -v g++ &> /dev/null; then
        missing="$missing gcc"
    fi
    
    if ! command -v make &> /dev/null; then
        missing="$missing make"
    fi
    
    if [ -n "$missing" ]; then
        print_error "Eksik paketler:$missing"
        echo "Yüklemek için: sudo pacman -S$missing"
        exit 1
    fi
    
    echo "  ✓ Tüm bağımlılıklar mevcut"
}

# Build dizinini oluştur
setup_build_dir() {
    print_step "Build dizini hazırlanıyor..."
    
    if [ -d "build" ]; then
        read -p "Build dizini mevcut. Temizlensin mi? (y/n) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            rm -rf build
        fi
    fi
    
    mkdir -p build
    echo "  ✓ Build dizini hazır"
}

# CMake yapılandırması
configure_cmake() {
    local build_type="${1:-Debug}"
    
    print_step "CMake yapılandırılıyor (${build_type})..."
    
    cd build
    cmake .. \
        -DCMAKE_BUILD_TYPE="${build_type}" \
        -DBUILD_TESTS=ON \
        -DBUILD_EXAMPLES=ON \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    
    cd ..
    echo "  ✓ CMake yapılandırması tamamlandı"
}

# Derleme
build_project() {
    print_step "Proje derleniyor..."
    
    cd build
    make -j$(nproc)
    cd ..
    
    echo "  ✓ Derleme tamamlandı"
}

# Testleri çalıştır
run_tests() {
    print_step "Testler çalıştırılıyor..."
    
    cd build
    ctest --output-on-failure
    cd ..
    
    echo "  ✓ Testler tamamlandı"
}

# Demo çalıştır
run_demo() {
    print_step "Demo çalıştırılıyor..."
    
    if [ -f "build/bin/checkpoint_demo" ]; then
        ./build/bin/checkpoint_demo
    else
        print_warning "Demo executable bulunamadı"
    fi
}

# Temizlik
clean() {
    print_step "Temizlik yapılıyor..."
    
    rm -rf build
    rm -rf checkpoints
    rm -rf logs
    rm -rf simple_checkpoints
    rm -rf auto_save_checkpoints
    rm -rf partial_rollback_checkpoints
    
    echo "  ✓ Temizlik tamamlandı"
}

# Yardım mesajı
show_help() {
    echo "Kullanım: $0 [komut]"
    echo ""
    echo "Komutlar:"
    echo "  build       - Debug modda derle (varsayılan)"
    echo "  release     - Release modda derle"
    echo "  test        - Testleri çalıştır"
    echo "  demo        - Demo uygulamayı çalıştır"
    echo "  clean       - Build dosyalarını temizle"
    echo "  all         - Temizle, derle ve test et"
    echo "  help        - Bu yardım mesajını göster"
    echo ""
}

# Ana akış
main() {
    local command="${1:-build}"
    
    case "$command" in
        build)
            check_dependencies
            setup_build_dir
            configure_cmake "Debug"
            build_project
            echo ""
            echo -e "${GREEN}Build başarılı!${NC}"
            echo "Demo için: ./build.sh demo"
            echo "Test için: ./build.sh test"
            ;;
        release)
            check_dependencies
            setup_build_dir
            configure_cmake "Release"
            build_project
            echo ""
            echo -e "${GREEN}Release build başarılı!${NC}"
            ;;
        test)
            if [ ! -d "build" ]; then
                print_error "Önce derleme yapın: ./build.sh build"
                exit 1
            fi
            run_tests
            ;;
        demo)
            if [ ! -d "build" ]; then
                print_error "Önce derleme yapın: ./build.sh build"
                exit 1
            fi
            run_demo
            ;;
        clean)
            clean
            ;;
        all)
            clean
            check_dependencies
            setup_build_dir
            configure_cmake "Debug"
            build_project
            run_tests
            echo ""
            echo -e "${GREEN}Tüm işlemler başarılı!${NC}"
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            print_error "Bilinmeyen komut: $command"
            show_help
            exit 1
            ;;
    esac
}

# Script'i çalıştır
main "$@"

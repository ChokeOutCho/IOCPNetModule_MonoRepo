## Prerequisites (요구 사항)

이 프로젝트를 빌드하고 실행하려면 다음 환경이 필요합니다.

1. **[Docker](https://www.docker.com/)**: MySQL 및 Redis 서버 실행 환경
2. **[vcpkg](https://github.com/microsoft/vcpkg)**: C++ 패키지 관리자 
   * C++ 애플리케이션에서 데이터베이스와 통신하기 위한 **MySQL 및 Redis C++ 클라이언트 라이브러리(예: `libmysql` 등)**를 설치하는 데 사용됩니다.
   * [vcpkg 설치 및 사용법 가이드 (Microsoft 공식 문서)](https://learn.microsoft.com/ko-kr/vcpkg/get_started/overview)

---

## Database & Cache Setup (Docker)

이 프로젝트는 데이터베이스(MySQL)와 인메모리 캐시(Redis)를 Docker 컨테이너로 구동합니다. 
저장소 최상단에 포함된 `docker-compose.yml`을 사용하여 손쉽게 환경을 구성할 수 있습니다.

프로젝트 루트 디렉터리에서 아래 명령어를 실행하여 컨테이너를 백그라운드에서 시작합니다.

```bash
docker-compose up -d
program TestSpecUtils
    use testdrive, only : error_type, check
   implicit none

    call DerivationPairMap()
    print *, "Success!"
    contains

subroutine DerivationPairMap()
    type(error_type), allocatable :: error
    call check(error, 0, 0)
end subroutine
end program
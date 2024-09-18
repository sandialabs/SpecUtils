program TestSpecUtils
    use testdrive, only : error_type, check
    use specutilswrap
    implicit none

    call SpecUtilsRoundTrip()
    call DerivationPairMap()
    print *, "Success!"
    contains

subroutine SpecUtilsRoundTrip()
!    use, intrinsic :: ISO_C_BINDING
    type(error_type), allocatable :: error
    type(SpecFile) :: sf
    type(Measurement) :: m
    character(len=:), allocatable :: filePath
    character(len=:), allocatable :: title
    integer :: istat, i
    real :: sum
    logical :: success
    real, dimension(:), allocatable :: spectrum
    type(FloatVector) :: coeffsIn
    type(DeviationPairs) :: devPairsIn
    type(DevPair) :: devPairVal
    type(EnergyCalibration) :: ecalIn

    filePath = "spec-utils-round-trip.pcf"
    if ( is_file(filePath) ) then 
        success = remove_file( filePath )
        call check( error, success )
    end if    

    m = Measurement()

    call m%set_start_time_from_string("14-Nov-1995 12:17:41.43")
    call m%set_title("SpecUtilsRoundTrip")
    call m%set_description("TestDescription")
    call m%set_source("TestSource")
    call m%set_neutron_count(99.0)

    allocate( spectrum(128) )
    DO i = 1, 128
        spectrum(i) = i
    END DO
    call m%set_spectrum( spectrum )

    call m%set_live_time(10.55)
    call m%set_real_time(11.66)

    coeffsIn = FloatVector()
    call coeffsIn%push_back(4.41)
    call coeffsIn%push_back(3198.33)
    call coeffsIn%push_back(1.0)
    call coeffsIn%push_back(2.0)

    devPairsIn = DeviationPairs()

    do i=1,4
        devPairVal = DevPair(i+10.0, i*(-1.0))
        call devPairsIn%push_back(devPairVal)
        call devPairVal%release()
    end do

    ecalIn = EnergyCalibration()
    call ecalIn%set_full_range_fraction(size(spectrum), coeffsIn, devPairsIn)
    call m%set_energy_calibration(ecalIn)

    sf = SpecFile()
    call sf%add_measurement(m)
    call sf%write_to_file(filePath, SaveSpectrumAsType_Pcf) 

    call SpecUtilsRoundTrip_Read(sf, filePath)

    call sf%release()
    call coeffsIn%release()
    call devPairsIn%release()
    call ecalIn%release()
    call m%release()

end subroutine

subroutine SpecUtilsRoundTrip_Read(expectedSpecFile, filePath)
    type(SpecFile), intent(in) :: expectedSpecFile
    type(error_type), allocatable :: error
    type(SpecFile) :: actualSpecFile
    type(Measurement) :: expM, actM
    character(len=:), allocatable, intent(in) :: filePath
    character(len=:), allocatable :: title
    integer :: istat, i, numChannels
    logical :: success
    real, dimension(:), allocatable :: expSpectrum, actSpectrum
    type(EnergyCalibration) :: ecalAct, ecalExp
    type(FloatVector) :: coeffsAct, coeffsExp
    type(DeviationPairs) :: devPairsAct, devPairsExp
    type(DevPair) :: devPairAct, devPairExp

    actualSpecFile = SpecFile()
    success = actualSpecFile%load_file(filePath, ParserType_Auto)
    call check(error, success)

    expM = expectedSpecFile%measurement_at(1)
    actM = actualSpecFile%measurement_at(1)

    call check(error, actM%title(), expM%title() )
    call check(error, actM%get_description(), expM%get_description() )
    call check(error, actM%get_source(), expM%get_source() )
    call check(error, actM%get_start_time_string(), expM%get_start_time_string() )
    call check(error, actM%live_time() .gt. 0.0)
    call check(error, actM%live_time(), expM%live_time() )
    
    call check(error, actM%get_neutron_count() .gt. 0.0 )    
    call check(error, actM%get_neutron_count(), expM%get_neutron_count() )

    numChannels = expM%get_num_channels()
    allocate(expSpectrum(numChannels))
    call expM%get_spectrum(expSpectrum)
    call check(error, size(expSpectrum), 128)

    numChannels = actM%get_num_channels()
    allocate(actSpectrum(numChannels))
    call actM%get_spectrum(actSpectrum)
    call check(error, size(actSpectrum), 128)

    do i=1, numChannels
        call check( error, actSpectrum(i) .gt. 0 )
        call check( error, actSpectrum(i), expSpectrum(i) )
    end do

    ecalAct = actM%energy_calibration()
    ecalExp = expM%energy_calibration()

    coeffsAct = ecalAct%coefficients()
    coeffsExp = ecalExp%coefficients()

    do i = 1,coeffsExp%size()
        call check(error, coeffsAct%get(i) .gt. 0.0)
        call check(error, coeffsAct%get(i), coeffsExp%get(i))
    end do    

    devPairsAct = ecalAct%deviation_pairs()
    devPairsExp = ecalExp%deviation_pairs()

    do i = 1, devPairsExp%size()
        devPairAct = devPairsAct%get(i)
        devPairExp = devPairsExp%get(i)

        call check(error, devPairAct%get_first(), devPairExp%get_first())
        call check(error, devPairAct%get_second(), devPairExp%get_second())
    end do

    call ecalAct%release()
    call ecalExp%release()
    call expM%release()
    call actM%release()
    call actualSpecFile%release()
end subroutine


subroutine DerivationPairMap()
    type(error_type), allocatable :: error
    call check(error, 0, 0)
end subroutine
end program
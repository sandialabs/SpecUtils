program TestSpecUtils
    use testdrive, only : error_type, check
    use specutilswrap
    implicit none
    integer, parameter :: maxDevPairs = 20
    integer, parameter :: maxMCA = 8
    integer, parameter :: maxPanel = 8
    integer, parameter :: maxCol = 4

    call SpecUtilsRoundTrip()
    call DerivationPairMap()
    print *, "Success!"
    contains

subroutine mapDevPairsToArray2(specFile, fortranArray)
    implicit none
    type(PcfFile), intent(in) :: specFile
    real :: fortranArray(2, 20, 8, 8, 4)
    integer :: numMeasurements, i, devPairIdx
    type(MeasurementExt) :: m
    integer :: column, panel, mca, numDevPairs
    type(DevPair) :: pair
    type(DeviationPairs) :: devPairs

    numMeasurements = specFile%num_measurements()

    do i = 1, numMeasurements
        m = specFile%get_measurement_at(i-1)
        column = m%column()
        panel = m%panel()
        mca = m%mca()
        devPairs = m%deviation_pairs()
        devPairIdx = 1

        numDevPairs = devPairs%size()
        do devPairIdx = 1, numDevPairs
            pair = devPairs%get(devPairIdx)
            fortranArray(1, devPairIdx, mca + 1, panel + 1, column + 1) = pair%get_first()
            fortranArray(2, devPairIdx, mca + 1, panel + 1, column + 1) = pair%get_second()
        end do
    end do
end subroutine 

subroutine SpecUtilsRoundTrip()
!    use, intrinsic :: ISO_C_BINDING
    type(error_type), allocatable :: error
    type(PcfFile) :: sf
    type(MeasurementExt) :: m
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
    real :: devPairArray(2, maxDevPairs, maxMCA, maxPanel, maxCol)

    filePath = "spec-utils-round-trip.pcf"
    if ( is_file(filePath) ) then 
        success = remove_file( filePath )
        call check( error, success )
    end if    

    m = MeasurementExt()

    call m%set_start_time_from_string("14-Nov-1995 12:17:41.43")
    call m%set_title("SpecUtilsRoundTrip Det=Ba2")
    call m%set_detector_name("Ba2");
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

    sf = PcfFile()
    call sf%add_measurement(m)
    call sf%write_to_file(filePath, SaveSpectrumAsType_Pcf) 

    call SpecUtilsRoundTrip_Read(sf, filePath)

    call mapDevPairsToArray2(sf, devPairArray)

    call check( error, devPairArray(1, 1, 2, 2, 1), 11.0 )
    call check( error, devPairArray(2, 1, 2, 2, 1), -1.0 )

    call sf%release()
    call coeffsIn%release()
    call devPairsIn%release()
    call ecalIn%release()
    call m%release()

end subroutine

subroutine SpecUtilsRoundTrip_Read(expectedSpecFile, filePath)
    type(PcfFile), intent(in) :: expectedSpecFile
    type(error_type), allocatable :: error
    type(PcfFile) :: actualSpecFile
    type(MeasurementExt) :: expM, actM
    character(len=:), allocatable, intent(in) :: filePath
    character(len=:), allocatable :: title
    integer :: istat, i, numChannels
    logical :: success
    real, dimension(:), allocatable :: expSpectrum, actSpectrum
    type(EnergyCalibration) :: ecalAct, ecalExp
    type(FloatVector) :: coeffsAct, coeffsExp
    type(DeviationPairs) :: devPairsAct, devPairsExp
    type(DevPair) :: devPairAct, devPairExp

    actualSpecFile = PcfFile()
    success = actualSpecFile%load_file(filePath, ParserType_Auto)
    call check(error, success)

    expM = expectedSpecFile%get_measurement_at(0)
    actM = actualSpecFile%get_measurement_at(0)

    call check(error, actM%title(), expM%title() )
    call check(error, actM%get_description() .ne. '' )    
    call check(error, actM%get_description(), expM%get_description() )
    call check(error, actM%get_source() .ne. '' )    
    call check(error, actM%get_source(), expM%get_source() )
    !call check(error, actM%get_column(), expM%get_column() )
    !call check(error, actM%get_panel(), expM%get_panel() )
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

subroutine getExpectedDeviationPair(col, panel, mca, devPair, first, second)
    integer, intent(in) :: col, panel, mca, devPair
    real, intent(out) :: first, second
    integer :: totalPairs, pairVal

    ! Validate the indices to ensure they are within bounds
    if (col >= maxCol .or. panel >= maxPanel .or. mca >= maxMCA .or. devPair >= maxDevPairs) then
        print *, "Index out of range"
        stop
    end if

    ! Calculate the total number of pairs before the given indices
    totalPairs = devPair-1 + (mca-1) * maxDevPairs + (panel-1) * maxDevPairs * maxMCA + (col-1) * maxDevPairs * maxMCA * maxPanel

    ! Calculate the pairVal
    pairVal = 1 + 2 * totalPairs

    ! Calculate the first and second pair values
    first = real(pairVal)
    second = real(pairVal + 1)
end subroutine 

subroutine getDetectorName(panel, column, mca, isNeutron, detectorName)
    integer, intent(in) :: panel, column, mca
    logical, intent(in), optional :: isNeutron
    character(len=20), intent(out) :: detectorName
    character(len=1) :: panelChar, columnChar, mcaChar

    ! Validate input parameters
    if (panel < 1 .or. column < 1 .or. mca < 1) then
        print *, "Error: Panel, column, and MCA numbers must be greater than 0."
        stop
    end if

    ! Convert panel, column, and MCA to the appropriate characters
    panelChar = char(iachar('A') + (panel - 1))
    columnChar = char(iachar('a') + (column - 1))
    mcaChar = char(iachar('1') + (mca - 1))

    ! Construct the detector name
    detectorName = panelChar // columnChar // mcaChar

    ! Append 'N' if it's a neutron detector
    if (present(isNeutron) .and. isNeutron) then
        detectorName = detectorName // 'N'
    end if

end subroutine

subroutine DerivationPairMap()
    type(error_type), allocatable :: error
    type(PcfFile) :: sf
    type(EnergyCalibrationExt) :: ecal
    type(MeasurementExt) :: m
    type(DeviationPairs) :: devPairs
    type(DevPair) :: devPairAct, devPairExp, dp
    character(len=20) :: detectorName
    real :: devPairArray(2, maxDevPairs, maxMCA, maxPanel, maxCol)
    integer :: col_i, panel_j, mca_k, p
    real :: first, second
    integer :: pairVal
    integer :: col, panel, mca, devPair_i

    sf = PcfFile()
    pairVal = 0
    do col = 1, maxCol
        do panel = 1, maxPanel
            do mca = 1, maxMCA
                m = MeasurementExt()
                devPairs = DeviationPairs()
                call getDetectorName(panel,col,mca,.false.,detectorName)
                call m%set_detector_name(detectorName)
                do devPair_i = 1, maxDevPairs
                    dp = DevPair()
                    pairVal = pairVal + 1
                    call dp%set_first(real(pairVal))
                    pairVal = pairVal + 1
                    call dp%set_second(real(pairVal))
                    call devPairs%push_back(dp)                      
                end do
                ecal = EnergyCalibrationExt()
                call ecal%set_dev_pairs(devPairs)
                call m%set_ecal(ecal)
                call sf%add_measurement(m)
            end do
        end do
    end do

    call mapDevPairsToArray2(sf, devPairArray)

    col =3
    panel = 2
    mca = 7
    devPair_i = 19
    call getExpectedDeviationPair(col, panel, mca, devPair_i, first, second)

    call check( error, devPairArray(1,devPair_i,mca,panel,col), first)
    call check( error, devPairArray(2,devPair_i,mca,panel,col), second)
    
end subroutine

end program
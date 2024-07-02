/* SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 
 Copyright 2018 National Technology & Engineering Solutions of Sandia, LLC
 (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
 Government retains certain rights in this software.
 For questions contact William Johnson via email at wcjohns@sandia.gov, or
 alternative emails of interspec@sandia.gov, or srb@sandia.gov.
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* File created by Edward Walsh. */

import java.awt.Dimension;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.io.File;
import java.io.IOException;
import javax.swing.JButton;
import javax.swing.JFileChooser;
import javax.swing.JFrame;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTextArea;
import javax.swing.JTextField;

import gov.sandia.specutils.FloatVector;
import gov.sandia.specutils.IntVector;
import gov.sandia.specutils.Measurement;
import gov.sandia.specutils.ParserType;
import gov.sandia.specutils.SWIGTYPE_p_std__ostream;
import gov.sandia.specutils.SaveSpectrumAsType;
import gov.sandia.specutils.SpecFile;
import gov.sandia.specutils.SpecUtilsSwig;
import gov.sandia.specutils.StringVector;
import gov.sandia.specutils.D3SpectrumOptions;
import gov.sandia.specutils.D3SpectrumChartOptions;
import gov.sandia.specutils.EnergyCalType;
import gov.sandia.specutils.EnergyCalibration;
import org.jfree.chart.ChartFactory;
import org.jfree.chart.ChartPanel;
import org.jfree.chart.JFreeChart;
import org.jfree.data.category.CategoryDataset;
import org.jfree.data.general.DatasetUtilities;

import org.joda.time.DateTimeZone;

/*************************************************************************
 * 
 * cd SpecUtils
 * mkdir build
 * cd build
 * cmake -DSpecUtils_JAVA_SWIG=ON ..
 * make -j4
 *
 * #To then run the example Java executable, do:
 * cp ../bindings/swig/java_example/* .
 * javac -classpath .:jcommon-1.0.21.jar:jfreechart-1.0.17.jar:joda-time-2.9.jar *.java gov/sandia/specutils/*.java
 * java -Djava.library.path="." -classpath .:jcommon-1.0.21.jar:jfreechart-1.0.17.jar:joda-time-2.9.jar Main
 *
 * Notes:
 *  To find swig .i files, look here:
 *    /usr/local/share/swig/3.0.7
 *    /usr/local/share/swig/3.0.7/java
 *
 *************************************************************************/

public class Main extends JFrame implements ActionListener{


     static {
         try {
             System.loadLibrary("SpecUtilsJni");
         }
         catch (UnsatisfiedLinkError e) {
             System.err.println(e.toString());
             System.exit(-1);
         }//catch
     }//static

     JPanel panelChart = null;
     JFreeChart chart = null;


     /* create an an instance of the SpecFile class */
     public SpecFile specFile = new SpecFile();
     public JTextField textFilename = new JTextField();
     public JScrollPane scrollPaneInfo = new JScrollPane();
     public JTextArea textInfo = new JTextArea();
     public JScrollPane scrollPaneMeasurementInfo = new JScrollPane();
     public JTextArea textMeasurementInfo = new JTextArea();
     public JTextField textIndexMeasurementPrompt = new JTextField();
     public JTextField textIndexMeasurement = new JTextField();





public Main(){

    /* load a data file */
    //this.filename = selectFile("Data_Bell_412_HPGe_lines_directly_over_US_Ecology_site.n42");
    //System.out.println("filename=" + filename);



	this.setSize(new Dimension(800,600));
	this.setPreferredSize(new Dimension(800,600));
	this.setVisible(true);
	//this.setBackground(Color.BLUE);
	this.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
	this.getContentPane().setLayout(null);

    //textFilename = new JTextField();
    textFilename.setLocation(0,10);
    textFilename.setSize(400,25);
    textFilename.setPreferredSize(new Dimension(400,25));
    this.getContentPane().add(textFilename);

    JButton buttonBrowse = new JButton("browse");
    buttonBrowse.setSize(100,25);
    buttonBrowse.setPreferredSize(new Dimension(100,25));
    buttonBrowse.setLocation(450,10);
    this.getContentPane().add(buttonBrowse);
    buttonBrowse.addActionListener(this);

    //textInfo = new JTextAread();
    //textInfo.setLocation(0,40);
    //textInfo.setSize(400,100);
    //textInfo.setPreferredSize(new Dimension(400,100));
    //this.getContentPane().add(textInfo);

    //JScrollPane scrollPaneInfo = new JScrollPane();
    scrollPaneInfo.setLocation(5,40);
    scrollPaneInfo.setSize(350,100);
    scrollPaneInfo.setPreferredSize(new Dimension(350,100));
    scrollPaneInfo.setHorizontalScrollBarPolicy(JScrollPane.HORIZONTAL_SCROLLBAR_ALWAYS);
    scrollPaneInfo.setVerticalScrollBarPolicy(JScrollPane.VERTICAL_SCROLLBAR_ALWAYS);
    scrollPaneInfo.getViewport().add(textInfo);
    scrollPaneInfo.setVisible(false);;
    this.getContentPane().add(scrollPaneInfo);

    //JScrollPane scrollPaneMeasurementInfo = new JScrollPane();
    scrollPaneMeasurementInfo.setLocation(420,40);
    scrollPaneMeasurementInfo.setSize(350,100);
    scrollPaneMeasurementInfo.setPreferredSize(new Dimension(350,100));
    scrollPaneMeasurementInfo.setHorizontalScrollBarPolicy(JScrollPane.HORIZONTAL_SCROLLBAR_ALWAYS);
    scrollPaneMeasurementInfo.setVerticalScrollBarPolicy(JScrollPane.VERTICAL_SCROLLBAR_ALWAYS);
    scrollPaneMeasurementInfo.getViewport().add(textMeasurementInfo);
    scrollPaneMeasurementInfo.setVisible(false);
    this.getContentPane().add(scrollPaneMeasurementInfo);



    //JTextField textIndexMeasurementPrompt = new JTextField("",20);
    textIndexMeasurementPrompt.setLocation(5,160);
    textIndexMeasurementPrompt.setSize(200,25);
    textIndexMeasurementPrompt.setPreferredSize(new Dimension(200,25));
    textIndexMeasurementPrompt.setBorder(null);
    textIndexMeasurementPrompt.setBackground(this.getBackground());
    this.getContentPane().add(textIndexMeasurementPrompt);



    //JTextField textIndexMeasurement = new JTextField("",20);
    textIndexMeasurement.setLocation(210,160);
    textIndexMeasurement.setSize(50,25);
    textIndexMeasurement.setPreferredSize(new Dimension(50,25));
    this.getContentPane().add(textIndexMeasurement);
    textIndexMeasurement.addActionListener(this);
    textIndexMeasurement.setVisible(false);

    panelChart = new JPanel();
    panelChart.setLocation(0, 200);
    panelChart.setVisible(true);
    panelChart.setSize(800,360);
    panelChart.setPreferredSize(new Dimension(800,360));
    panelChart.setOpaque(true);
    //panel.setBackground(Color.YELLOW);
    //panel.setBorder(BorderFactory.createLineBorder(Color.GREEN, 10));
    this.getContentPane().add(panelChart);


    //this.pack();


    this.validate();

}

public String selectFile(String filename) {

  JFileChooser dialog = new JFileChooser();

  //set default folder & default filename
  dialog.setCurrentDirectory(new File("."));
  dialog.setSelectedFile(new File(".",filename));

  //display dialog box.
  dialog.showOpenDialog(null);

  //get folder & file that user selected
  File file = dialog.getSelectedFile();

  /* return the selected file */
  return(file.getAbsolutePath());
}







public void actionPerformed(ActionEvent event) {

	if (event.getSource() == this.textIndexMeasurement) textIndexMeasurement_actionPerformed(event);


	if (event.getActionCommand()=="browse") buttonBrowse_actionPerformed(event);




}





private void textIndexMeasurement_actionPerformed(ActionEvent event) {


	/* which measurement did the user select */
	int indexMeasurement = Integer.valueOf(event.getActionCommand());

	plotMeasurement();

	parseMeasurementInfo();


}

public void plotMeasurement() {

	/* which measurement did the user select */
	int indexMeasurement = Integer.valueOf(this.textIndexMeasurement.getText());
    Measurement measurement = specFile.measurement(indexMeasurement);

	/* get the gammaCounts in this measurement */
    FloatVector gammaCounts = measurement.gamma_counts();

    /* copy gammaCounts into an array */
    Float[] array = new Float[(int)gammaCounts.size()];
    for (int i=0; i<array.length; i++) {
    	array[i] = gammaCounts.get(i);
    }


    Number[][] data = new Float[1][];
    data[0] = array;

    CategoryDataset dataset = DatasetUtilities.createCategoryDataset("R","C", data);


	//create the chart
	JFreeChart chart = ChartFactory.createLineChart(
		"My Plot",  //title
		"X axis",   //X axis title
		"Y axis",   //y axis title
		dataset   //data set
		);

	//add the chart to a panel
	ChartPanel chartPanel = new ChartPanel(chart);
	chartPanel.setSize(800,350);
	chartPanel.setPreferredSize(new Dimension(800,350));
	//add the panel to our window
	panelChart.removeAll();
	panelChart.add(chartPanel);
	panelChart.validate();
}


/**
 */
public void buttonBrowse_actionPerformed(ActionEvent e) {

    selectFile();
    readFile();
}





public void parseMeasurementInfo() {

	this.textMeasurementInfo.setText("");
    this.scrollPaneMeasurementInfo.setVisible(true);

	/* which measurement did the user select */
	int indexMeasurement = Integer.valueOf(this.textIndexMeasurement.getText());
    Measurement measurement = specFile.measurement(indexMeasurement);

    //this.textMeasurementInfo.append("start time: " + measurement.start_time_copy().withZone(DateTimeZone.UTC).toString() + "\n");
    this.textMeasurementInfo.append("detector type: " + measurement.detector_type() + "\n");
    this.textMeasurementInfo.append("sample #: " + measurement.sample_number() + "\n");
    this.textMeasurementInfo.append("real time: " + measurement.real_time() + "\n");
    this.textMeasurementInfo.append("live time: " + measurement.live_time() + "\n");
    this.textMeasurementInfo.append("source type: " + measurement.source_type() + "\n");
    this.textMeasurementInfo.append("detector type: " + measurement.detector_type() + "\n");

    this.textMeasurementInfo.append("energy calibration model: " + measurement.energy_calibration_model() + "\n");
 
    this.textMeasurementInfo.append("calibration coefficients\n");
    for (int i=0; i<measurement.calibration_coeffs().size(); i++) {
    	this.textMeasurementInfo.append("    " + measurement.calibration_coeffs().get(i) + "\n");
    }


}



/**
 * Parse the selected file.
 * Display the extracted information.
 */
public void readFile() {

	this.scrollPaneInfo.setVisible(true);
	this.textIndexMeasurement.setVisible(true);

	/* We will display the extracted information here */
	this.textInfo.setText("");


	/* What file did the user select */
	String filename = this.textFilename.getText();

    /* Load the file */
    if (specFile.load_file( filename, ParserType.Auto, "" )==false) {
        this.textInfo.append("ERROR:  Can't read " + filename + "\n");
        return;
     }//if


    /* get remarks */
    for (int i=0; i<specFile.remarks().size(); i++) {
    	this.textInfo.append(specFile.remarks().get(i) + "\n");
    }



    /* get the UUID */
    this.textInfo.append("UUID=" + specFile.uuid() + "\n");


    /* get instrument information */
    String s
      = "instrument: \n"
      + "    type="+specFile.instrument_type() + "\n"
      + "    manufacturer="+specFile.manufacturer()+"\n"
      + "    model="+specFile.instrument_model() + "\n"
      + "    id="+specFile.instrument_id()+"\n"
      ;
     this.textInfo.append(s + "\n");


     /* get the location */
     s = "location: "
        + "    " + specFile.measurement_location_name() + "\n"
    	+ "    latitude = " + specFile.mean_latitude() + "\n"
    	+ "    longitude = " + specFile.mean_longitude() + "\n"
    	;
     this.textInfo.append(s + "\n");


    /* get detector names and numbers */
    this.textInfo.append("detectors:" + "\n");
    StringVector detectorNames = specFile.detector_names();
    IntVector detectorNumbers = specFile.detector_numbers();
    s = "";
    for (int i=0; i<detectorNames.size(); i++) {
        String detectorName = detectorNames.get(i);
        s = "    name=" + detectorName;
        if (i<detectorNumbers.size()) {
        	int detectorNumber = detectorNumbers.get(i);
        	s =  s + "    number="+detectorNumber;
        }
        this.textInfo.append(s + "\n");
    }



    /* get number of measurements */
    long numberOfMeasurements = specFile.num_measurements();
    this.textInfo.append("number of measurements = " + numberOfMeasurements + "\n");

    this.textIndexMeasurementPrompt.setText("select measurement (0-" + (numberOfMeasurements-1) + ")");

    /* write a PCF file */
    SWIGTYPE_p_std__ostream file = SpecUtilsSwig.openFile("data_generatedFile.pcf");
    specFile.write_pcf(file);
    SpecUtilsSwig.closeFile(file);
    this.textInfo.append("wrote data_generatedFile.pcf\n");
    
    /* Write another PCF file */
    SaveSpectrumAsType format = SaveSpectrumAsType.Pcf;
    specFile.write_to_file("data_generatedFile2.pcf", format);
    this.textInfo.append("wrote data_generatedFile2.pcf\n");
    
    /* Write a N42 file */
    format = SaveSpectrumAsType.N42_2012;
    IntVector sample_nums = new IntVector();
    sample_nums.add(1);
    IntVector det_nums = new IntVector();
    det_nums.add(0);
    specFile.write_to_file("data_generatedFile.n42", sample_nums, det_nums, format);
    this.textInfo.append("wrote data_generatedFile.n42\n");

    /* Write HTML file */ 
    SWIGTYPE_p_std__ostream htmlfile = SpecUtilsSwig.openFile("data_generatedFile.html");
    D3SpectrumChartOptions htmlOptions = new D3SpectrumChartOptions();
    specFile.write_d3_html(htmlfile, htmlOptions, specFile.sample_numbers(), specFile.detector_names() );
    SpecUtilsSwig.closeFile(htmlfile);
    this.textInfo.append("wrote data_generatedFile.html\n");
}






/**
 * Pop up a file dialog box to allow the user to select a file.
 */
public void selectFile() {


	this.scrollPaneMeasurementInfo.setVisible(false);
	this.panelChart.removeAll();

	/* What filename is in the text box? */
    java.io.File file = null;
    String folderName = ".";
    String filename = "";
    String fullFilename = this.textFilename.getText();
    if (fullFilename.trim().equals("")==false) {
        file = new java.io.File(fullFilename);
        try {
            file = new java.io.File(file.getCanonicalPath());
        } catch (IOException ex1) {
            file = new java.io.File(file.getAbsolutePath());
        }
        folderName = file.getParent();
        filename = file.getName();
    }

    /* create a dialog box */
    JFileChooser dialog = new JFileChooser();

    //set default folder & default filename
    dialog.setCurrentDirectory(new java.io.File(folderName));
    dialog.setSelectedFile(file);

    //display dialog box. Did user press CANCEL button?
    if (dialog.showOpenDialog(null) == JFileChooser.CANCEL_OPTION) {
        return;
    }

    //get folder & file that user selected
    file = dialog.getSelectedFile();


    /* insert the name of the selected file into our textbox */
    try {
        this.textFilename.setText(file.getCanonicalPath());
    } catch (IOException ex) {
        this.textFilename.setText(file.getAbsolutePath());
    }


}








public static void main(String args[]) {


	javax.swing.SwingUtilities.invokeLater(new Runnable(){
		public void run() {
			new Main();
		}
	});

}//main



}//class

#include "pch-seek.h"
#include <config.h>

#include "toolkit.h"

#include <psrxml.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <string.h>

char matches_sigproc(float p1, float p2);

/**
 * A 'dummy' operation that dumps spectra etc.
 */
void pch_seek_dump(float* data, int len, float xscale, char* filename){

	FILE* file;
	int i;

	file = fopen(filename,"w");

	for(i=0; i < len; i++){
		fprintf(file,"%f %f\n",xscale*i,data[i]);
	}

	fclose(file);

	return;
}

void pch_seek_histogram(float* data, int len, int hist_bins, char* filename){

	FILE* file;
	int i;
	int *count;
	int hbin;
	float max,min,binsize;

	count = (int*)calloc(hist_bins,sizeof(int));

	max = -1e10;
	min = 1e10;


	for(i=0; i < len; i++){
		if(min > data[i])min = data[i];
		if(max < data[i])max = data[i];
	}

	for(i=0; i < len; i++){
		hbin=(int)(((data[i]-min)/(max-min))*hist_bins);
		count[hbin]++;
	}

	file = fopen(filename,"w");

	binsize = (max-min)/(float)(hist_bins);

	for(i=0; i < hist_bins; i++){
		fprintf(file,"%f %d\n",(float)i*binsize + min,count[i]);
	}

	fclose(file);

	return;
}

void pch_seek_write_presto_fft(char* filestem, fftwf_complex* spectrum, int ncomplex, psrxml* header){
	
	FILE* file;
	int ret;
	unsigned long long mjdfrac;
	char* filename;
	char angle[128];

	filename = (char*) malloc(sizeof(char) * (strlen(filestem)+5));
	sprintf(filename,"%s.fft",filestem);
	file=fopen(filename,"w");
	if (file==NULL){
		fprintf(stderr,"Error opening presto fft binary file '%s'.\nErr: %d, %s\n",filename,errno,strerror(errno));
		exit(1);
	}
	ret=fwrite(spectrum,sizeof(fftwf_complex),ncomplex,file);
	if (ret != ncomplex){
		fprintf(stderr,"Error writing raw spectrum to presto fft binary file '%s'.\nErr: %d, %s\n",filename,errno,strerror(errno));
		exit(1);
	}
	fclose(file);
	sprintf(filename,"%s.inf",filestem);
	file=fopen(filename,"w");
	if (file==NULL){
		fprintf(stderr,"Error opening presto header file '%s'!\nErr: %d, %s\n",filename,errno,strerror(errno));
		exit(1);
	}
	/*
	 * Convert from psrxml headers to presto inf file!
	 */
	fprintf(file," Data file name without suffix          =  %s\n",filestem);
	fprintf(file," Telescope used                         =  %s\n",header->telescope.name);
	fprintf(file," Instrument used                        =  %s\n",header->receiverIdentifyingString);
	fprintf(file," Object being observed                  =  %s\n",header->sourceName);
	deg2hms(header->startCoordinate.ra,angle,4);
	fprintf(file," J2000 Right Ascension (hh:mm:ss.ssss)  =  %s\n",angle);
	deg2sex(header->startCoordinate.dec,angle,4);
	fprintf(file," J2000 Declination     (dd:mm:ss.ssss)  =  %s\n",angle);

	fprintf(file," Data observed by                       =  %s\n",header->observerName);
	mjdfrac = 1000000000000000LL*(header->timeToFirstSample/(double)8.64e13);
	fprintf(file," Epoch of observation (MJD)             =  %u.%015llu\n",header->mjdObs,mjdfrac);
	fprintf(file," Barycentered?           (1=yes, 0=no)  =  0\n");
	fprintf(file," Number of bins in the time series      =  %u\n",header->numberOfSamples);
	fprintf(file," Width of each time series bin (sec)    =  %lf\n",header->currentSampleInterval);
	fprintf(file," Any breaks in the data? (1=yes, 0=no)  =  0\n");
	fprintf(file," Type of observation (EM band)          =  Radio\n");
	fprintf(file," Beam diameter (arcsec)                 =  0\n");
	fprintf(file," Dispersion measure (cm-3 pc)           =  %f\n",header->referenceDm);
	/*
	 * Here there are only 1 chan per file!
	 */
	fprintf(file," Central freq of low channel (Mhz)      =  %lf\n",header->centreFreqCh1);
	fprintf(file," Total bandwidth (Mhz)                  =  %lf\n",header->freqOffset);
	fprintf(file," Number of channels                     =  1\n");
	fprintf(file," Channel bandwidth (Mhz)                =  %lf\n",header->freqOffset);
	fprintf(file," Data analyzed by                       =  NA\n");
	fprintf(file," Any additional notes:\n");
	fprintf(file,"    FFT'd by pch-seek using fftw3\n");
	fclose(file);
}

void pch_seek_write_prd(char* filename, float** freq, float** spec, float** recon, int* ncand, int* harms, int nharm, psrxml* header,char append){

	int maxncand=0;
	int ifold;
	int icand;
	int** indexes;
	float** sorter;

	FILE *file;

	if(append){
		file = fopen(filename, "a");
	} else {
		file = fopen(filename, "w");
	}

	indexes = (int**)malloc(sizeof(int*)*nharm);

	if(recon!=NULL)sorter=recon;
	else sorter=spec;

	for(ifold = 0; ifold < nharm; ifold++){
		// sort the candidates
		indexes[ifold] = (int*) malloc(sizeof(int)*ncand[ifold]);

		for (icand=0; icand < ncand[ifold]; icand++){
			indexes[ifold][icand]=icand;
		}
		quicksort_index(sorter[ifold],indexes[ifold],ncand[ifold]);
		// reverse the order of the array
		{
			int swap;
			int i,j;
			j=ncand[ifold];
			for(i=0;i<--j;i++){
				swap=indexes[ifold][i];
				indexes[ifold][i]=indexes[ifold][j];
				indexes[ifold][j]=swap;
			}
		}

		// remove freqs that are very close
		float* existing_freqs=(float*) malloc(sizeof(float)*ncand[ifold]);
		int nex=0;
		char found=0;
		int remd=0;
		for (icand=0; icand < ncand[ifold] ; icand++){
			found = 0;

			for(int x=0; x < nex; x++){
				if(fabs(existing_freqs[x]/freq[ifold][indexes[ifold][icand]]-1) < 0.001 || 
						matches_sigproc(1.0/existing_freqs[x],1.0/freq[ifold][indexes[ifold][icand]])){

					// swap index to the end
					for(int y=icand+1; y < ncand[ifold]; y++){
						indexes[ifold][y-1]=indexes[ifold][y];
					}
					// we have one less cand
					ncand[ifold]--;
					// because we have changed which cand is at 'icand' we want to do it again
					icand--;
					// end the loop
					found=1;
					break;
				}
			}
			if(!found){
				existing_freqs[nex] = freq[ifold][indexes[ifold][icand]];
				nex++;
			}
		}
		if(maxncand < ncand[ifold])maxncand=ncand[ifold];

	}

	/*
	 * Add a header
	 *##BEGIN HEADER##
	 SOURCEID = GEGJ1027K
	 FREF =   3796.5 MHz
	 TSTART =   54562.2848
	 TELESCOPE = Parkes
	 RAJ = 10:28:20.2
	 DECJ = -58:20:11.1
	 TSAMP =    250.0 us
	 PROGRAM = SEEK
	 VERSION = 4.3                             
	 HARM_FOLDS =    1   2   4   8  16
	 COLS = SNR_SPEC SNR_RECON PERIOD
##END HEADER##

*/
	if (!append){

		fprintf(file,"##BEGIN HEADER##\n");
		fprintf(file,"SOURCEID = %s\n",header->sourceName);
		fprintf(file,"FREF = %f MHz\n",header->centreFreqCh1);
		fprintf(file,"TSTART = %f\n",header->mjdObs);
		fprintf(file,"TELESCOPE = %s\n",header->telescope.name);

		fprintf(file,"RAJ = %f\n",header->startCoordinate.ra);
		fprintf(file,"DECJ = %f\n",header->startCoordinate.dec);

		fprintf(file,"TSAMP = %f us\n",header->currentSampleInterval);
		fprintf(file,"PROGRAM = %s\n",PACKAGE_NAME);
		fprintf(file,"VERSION = %f\n",PACKAGE_VERSION);

		fprintf(file,"HARM_FOLDS =");
		for(ifold = 0; ifold < nharm; ifold++){
			fprintf(file," %d",harms[ifold]);
		}
		fprintf(file,"\n");

		if(recon==NULL){
			fprintf(file,"COLS = SNR_SPEC PERIOD\n");

		} else {
			fprintf(file,"COLS = SNR_SPEC SNR_RECON PERIOD\n");

		}



		fprintf(file,"##END HEADER##\n");
	}

	fprintf(file," DM:% 11.6f      AC:% 11.6f      AD:% 11.6f\n",header->referenceDm,0,0);

	for (icand=0; icand < maxncand; icand++){
		for(ifold = 0; ifold < nharm; ifold++){
			if(icand >= ncand[ifold]){
				// no more cands in this fold
				fprintf(file,"     0.0");
				if(recon!=NULL)fprintf(file,"     0.0");
				fprintf(file,"    1.00000000");
			} else {
				fprintf(file,"% 8.1f",spec[ifold][indexes[ifold][icand]]);
				if(recon!=NULL)fprintf(file,"% 8.1f",recon[ifold][indexes[ifold][icand]]);
				fprintf(file,"% 14.8f",1000.0/freq[ifold][indexes[ifold][icand]]);
			}
		}
		fprintf(file,"\n");
	}

	fclose(file);

	for(ifold = 0; ifold < nharm; ifold++){
		free(indexes[ifold]);
	}
	free(indexes);

}




char matches_sigproc(float p1, float p2){
	float ratio;
	char ret;
	ret = 0;

	ratio=p1/p2;
	if (ratio < 1.0) ratio=1.0/ratio;
	ratio=ratio-(int)(ratio);


	ret |= ratio > 0.999 || ratio < 0.001;
	ret |= ratio > 0.333 && ratio < 0.334;
	ret |= ratio > 0.499 && ratio < 0.501;
	ret |= ratio > 0.666 && ratio < 0.667;

	return ret;
}

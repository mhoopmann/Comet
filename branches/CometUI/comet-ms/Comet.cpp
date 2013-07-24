/*
   Copyright 2012 University of Washington

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/


#include "Common.h"
#include "CometData.h"
#include "CometMassSpecUtils.h"
#include "CometSearch.h"
#include "CometPreprocess.h"
#include "CometPostAnalysis.h"
#include "CometWriteOut.h"
#include "CometWriteSqt.h"
#include "CometWriteTxt.h"
#include "CometWritePepXML.h"
#include "CometSearchManager.h"
#include "Threading.h"
#include "ThreadPool.h"

#include <algorithm>

void Usage(int failure,
           char *pszCmd);
void ProcessCmdLine(int argc, 
                    char *argv[], 
                    char *szParamsFile);
void SetOptions(char *arg,
                char *szParamsFile,
                bool *bPrintParams);
void LoadParameters(char *pszParamsFile);
void PrintParams();
bool ValidateInputMsMsFile(char *pszInputFileName);


int main(int argc, char *argv[])
{
   char szParamsFile[SIZE_FILE];

   if (argc < 2) 
       Usage(0, argv[0]);

   ProcessCmdLine(argc, argv, szParamsFile);

   if (!g_staticParams.options.bOutputSqtStream
         && !g_staticParams.options.bOutputSqtFile
         && !g_staticParams.options.bOutputTxtFile
         && !g_staticParams.options.bOutputPepXMLFile
         && !g_staticParams.options.bOutputPinXMLFile
         && !g_staticParams.options.bOutputOutFiles)
   {
      printf("\n Comet version \"%s\"\n", comet_version);
      printf(" Please specify at least one output format.\n\n");
      exit(1);
   }

   CometSearchManager cometSearchMgr(szParamsFile);
   cometSearchMgr.DoSearch();

   return (0);

} // main


void Usage(int failure, char *pszCmd)
{
   printf("\n");
   printf(" Comet version \"%s\"\n %s\n", comet_version, copyright);
   printf("\n");
   printf(" Comet usage:  %s [options] <input_files>\n", pszCmd);
   printf("\n");
   printf(" Supported input formats include mzXML, mzXML, mz5 and ms2 variants (cms2, bms2, ms2)\n");
   printf("\n");
   printf("       options:  -p         to print out a comet.params file (named comet.params.new)\n");
   printf("                 -P<params> to specify an alternate parameters file (default comet.params)\n");
   printf("                 -N<name>   to specify an alternate output base name; valid only with one input file\n");
   printf("                 -D<dbase>  to specify a sequence database, overriding entry in parameters file\n");
   printf("                 -F<num>    to specify the first/start scan to search, overriding entry in parameters file\n");
   printf("                 -L<num>    to specify the last/end scan to search, overriding entry in parameters file\n");
   printf("                            (-L option is required if -F option is used)\n");
   printf("\n");
   printf("       example:  %s file1.mzXML file2.mzXML\n", pszCmd);
   printf("            or   %s -F1000 -L1500 file1.mzXML    <- to search scans 1000 through 1500\n", pszCmd);
   printf("            or   %s -pParams.txt *.mzXML         <- use parameters in the file 'Params.txt'\n", pszCmd);
   printf("\n");

   exit(1);
}

void SetOptions(char *arg,
      char *szParamsFile,
      bool *bPrintParams)
{
   char szTmp[512];

   switch (arg[1])
   {
      case 'D':   // Alternate sequence database.
         if (sscanf(arg+2, "%512s", szTmp) == 0)
            fprintf(stderr, "Cannot read command line database: '%s'.  Ignored.\n", szTmp);
         else
            strcpy(g_staticParams.databaseInfo.szDatabase, szTmp);
         break;
      case 'P':   // Alternate parameters file.
         if (sscanf(arg+2, "%512s", szTmp) == 0 )
            fprintf(stderr, "Missing text for parameter option -P<params>.  Ignored.\n");
         else
            strcpy(szParamsFile, szTmp);
         break;
      case 'N':   // Set basename of output file (for .out, SQT, and pepXML)
         if (sscanf(arg+2, "%512s", szTmp) == 0 )
            fprintf(stderr, "Missing text for parameter option -N<basename>.  Ignored.\n");
         else
            strcpy(g_staticParams.inputFile.szBaseName, szTmp);
         break;
      case 'F':
         if (sscanf(arg+2, "%512s", szTmp) == 0 )
            fprintf(stderr, "Missing text for parameter option -F<num>.  Ignored.\n");
         else
            g_staticParams.options.iStartScan = atoi(szTmp);
         break;
      case 'L':
         if (sscanf(arg+2, "%512s", szTmp) == 0 )
            fprintf(stderr, "Missing text for parameter option -L<num>.  Ignored.\n");
         else
             g_staticParams.options.iEndScan = atoi(szTmp);
         break;
      case 'B':
         if (sscanf(arg+2, "%512s", szTmp) == 0 )
            fprintf(stderr, "Missing text for parameter option -B<num>.  Ignored.\n");
         else
             g_staticParams.options.iSpectrumBatchSize = atoi(szTmp);
         break;
      case 'p':
         *bPrintParams = true;
         break;
      default:
         break;
   }
}


// Reads comet.params parameter file.
void LoadParameters(char *pszParamsFile)
{
   double dTempMass;
   int   i,
         iSearchEnzymeNumber,
         iSampleEnzymeNumber;
   char  szParamBuf[SIZE_BUF],
         szParamName[128],
         szParamVal[128],
         szVersion[128];
   FILE  *fp;
   bool  bCurrentParamsFile = 0, // Track a parameter to make sure present.
         bValidParamsFile;
   char *pStr;

   for (i=0; i<SIZE_MASS; i++)
      g_staticParams.staticModifications.pdStaticMods[i] = 0.0;

   if ((fp=fopen(pszParamsFile, "r")) == NULL)
   {
      fprintf(stderr, "\n Comet version %s\n %s\n", comet_version, copyright);
      fprintf(stderr, " Error - cannot open parameter file \"%s\".\n\n", pszParamsFile);
      exit(1);
   }

   // validate not using incompatible params file by checking "# comet_version" in first line of file
   strcpy(szVersion, "unknown");
   bValidParamsFile = false;
   while (!feof(fp))
   {
      fgets(szParamBuf, SIZE_BUF, fp);
      if (!strncmp(szParamBuf, "# comet_version ", 16))
      {
         sscanf(szParamBuf, "%*s %*s %128s", szVersion);
         // Major version number must match to current binary
         if (strstr(comet_version, szVersion))
         {
            bValidParamsFile = true;
            break;
         }
      }
   }

   if (!bValidParamsFile)
   {
      printf("\n");
      printf(" Comet version is %s\n", comet_version);
      printf(" The comet.params file is from version %s\n", szVersion);
      printf(" Please update your comet.params file.  You can generate\n");
      printf(" a new parameters file using \"comet -p\"\n");
      printf("\n");
      exit(1);
   }

   rewind(fp);

   // now parse the parameter entries
   while (!feof(fp))
   {
      fgets(szParamBuf, SIZE_BUF, fp);

      if (!strncmp(szParamBuf, "[COMET_ENZYME_INFO]", 19))
         break;

      if (! (szParamBuf[0]=='#' || (pStr = strchr(szParamBuf, '='))==NULL))
      {
         strcpy(szParamVal, pStr + 1);  // Copy over value.
         *pStr = 0;                     // Null rest of szParamName at equal char.

         sscanf(szParamBuf, "%128s", szParamName);

         if (!strcmp(szParamName, "database_name"))
         {
            sscanf(szParamVal, "%512s", g_staticParams.databaseInfo.szDatabase);
         }
         else if (!strcmp(szParamName, "nucleotide_reading_frame"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.iWhichReadingFrame));
         }
         else if (!strcmp(szParamName, "mass_type_parent"))
         {
            sscanf(szParamVal, "%d", &g_staticParams.massUtility.bMonoMassesParent);
         }
         else if (!strcmp(szParamName, "mass_type_fragment"))
         {
            sscanf(szParamVal, "%d", &g_staticParams.massUtility.bMonoMassesFragment);
         }
         else if (!strcmp(szParamName, "show_fragment_ions"))
         {
            sscanf(szParamVal, "%d",  &(g_staticParams.options.bPrintFragIons));
         }
         else if (!strcmp(szParamName, "num_threads"))
         {
            sscanf(szParamVal, "%d",  &(g_staticParams.options.iNumThreads));
         }
         else if (!strcmp(szParamName, "clip_nterm_methionine"))
         {
            sscanf(szParamVal, "%d",  &(g_staticParams.options.bClipNtermMet));
         }
         else if (!strcmp(szParamName, "theoretical_fragment_ions"))
         {
            sscanf(szParamVal, "%d", &g_staticParams.ionInformation.iTheoreticalFragmentIons);
            if ((g_staticParams.ionInformation.iTheoreticalFragmentIons < 0) || 
                (g_staticParams.ionInformation.iTheoreticalFragmentIons > 1))
            {
               g_staticParams.ionInformation.iTheoreticalFragmentIons = 0;
            }
         }
         else if (!strcmp(szParamName, "use_A_ions"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.ionInformation.iIonVal[ION_SERIES_A]));
         }
         else if (!strcmp(szParamName, "use_B_ions"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.ionInformation.iIonVal[ION_SERIES_B]));
         }
         else if (!strcmp(szParamName, "use_C_ions"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.ionInformation.iIonVal[ION_SERIES_C]));
         }
         else if (!strcmp(szParamName, "use_X_ions"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.ionInformation.iIonVal[ION_SERIES_X]));
         }
         else if (!strcmp(szParamName, "use_Y_ions"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.ionInformation.iIonVal[ION_SERIES_Y]));
         }
         else if (!strcmp(szParamName, "use_Z_ions"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.ionInformation.iIonVal[ION_SERIES_Z]));
         }
         else if (!strcmp(szParamName, "use_NL_ions"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.ionInformation.bUseNeutralLoss));
         }
         else if (!strcmp(szParamName, "use_sparse_matrix"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.bSparseMatrix));
         }
         else if (!strcmp(szParamName, "variable_mod1"))
         {
            sscanf(szParamVal, "%lf %20s %d %d",
                  &g_staticParams.variableModParameters.varModList[VMOD_1_INDEX].dVarModMass,
                  g_staticParams.variableModParameters.varModList[VMOD_1_INDEX].szVarModChar,
                  &g_staticParams.variableModParameters.varModList[VMOD_1_INDEX].bBinaryMod,
                  &g_staticParams.variableModParameters.varModList[VMOD_1_INDEX].iMaxNumVarModAAPerMod);
         }
         else if (!strcmp(szParamName, "variable_mod2"))
         {
            sscanf(szParamVal, "%lf %20s %d %d",
                  &g_staticParams.variableModParameters.varModList[VMOD_2_INDEX].dVarModMass,
                  g_staticParams.variableModParameters.varModList[VMOD_2_INDEX].szVarModChar,
                  &g_staticParams.variableModParameters.varModList[VMOD_2_INDEX].bBinaryMod,
                  &g_staticParams.variableModParameters.varModList[VMOD_2_INDEX].iMaxNumVarModAAPerMod);
         }
         else if (!strcmp(szParamName, "variable_mod3"))
         {
            sscanf(szParamVal, "%lf %20s %d %d",
                  &g_staticParams.variableModParameters.varModList[VMOD_3_INDEX].dVarModMass,
                  g_staticParams.variableModParameters.varModList[VMOD_3_INDEX].szVarModChar,
                  &g_staticParams.variableModParameters.varModList[VMOD_3_INDEX].bBinaryMod,
                  &g_staticParams.variableModParameters.varModList[VMOD_3_INDEX].iMaxNumVarModAAPerMod);
         }
         else if (!strcmp(szParamName, "variable_mod4"))
         {
            sscanf(szParamVal, "%lf %20s %d %d",
                  &g_staticParams.variableModParameters.varModList[VMOD_4_INDEX].dVarModMass,
                  g_staticParams.variableModParameters.varModList[VMOD_4_INDEX].szVarModChar,
                  &g_staticParams.variableModParameters.varModList[VMOD_4_INDEX].bBinaryMod,
                  &g_staticParams.variableModParameters.varModList[VMOD_4_INDEX].iMaxNumVarModAAPerMod);
         }
         else if (!strcmp(szParamName, "variable_mod5"))
         {
            sscanf(szParamVal, "%lf %20s %d %d",
                  &g_staticParams.variableModParameters.varModList[VMOD_5_INDEX].dVarModMass,
                  g_staticParams.variableModParameters.varModList[VMOD_5_INDEX].szVarModChar,
                  &g_staticParams.variableModParameters.varModList[VMOD_5_INDEX].bBinaryMod,
                  &g_staticParams.variableModParameters.varModList[VMOD_5_INDEX].iMaxNumVarModAAPerMod);
         }
         else if (!strcmp(szParamName, "variable_mod6"))
         {
            sscanf(szParamVal, "%lf %20s %d %d",
                  &g_staticParams.variableModParameters.varModList[VMOD_6_INDEX].dVarModMass,
                  g_staticParams.variableModParameters.varModList[VMOD_6_INDEX].szVarModChar,
                  &g_staticParams.variableModParameters.varModList[VMOD_6_INDEX].bBinaryMod,
                  &g_staticParams.variableModParameters.varModList[VMOD_6_INDEX].iMaxNumVarModAAPerMod);
         }
         else if (!strcmp(szParamName, "max_variable_mods_in_peptide"))
         {
            int iTmp = 0;
            sscanf(szParamVal, "%d", &iTmp);

            if (iTmp > 0)
               g_staticParams.variableModParameters.iMaxVarModPerPeptide = iTmp;
         }
         else if (!strcmp(szParamName, "fragment_bin_tol"))
         {
            sscanf(szParamVal, "%lf", &g_staticParams.tolerances.dFragmentBinSize);
            if (g_staticParams.tolerances.dFragmentBinSize < 0.01)
               g_staticParams.tolerances.dFragmentBinSize = 0.01;
         }
         else if (!strcmp(szParamName, "fragment_bin_offset"))
         {
            sscanf(szParamVal, "%lf", &g_staticParams.tolerances.dFragmentBinStartOffset);
         }
         else if (!strcmp(szParamName, "peptide_mass_tolerance"))
         {
            sscanf(szParamVal, "%lf",  &g_staticParams.tolerances.dInputTolerance);
         }
         else if (!strcmp(szParamName, "precursor_tolerance_type"))
         {
            sscanf(szParamVal, "%d", &g_staticParams.tolerances.iMassToleranceType);
            if ((g_staticParams.tolerances.iMassToleranceType < 0) || 
                (g_staticParams.tolerances.iMassToleranceType > 1))
            {
                g_staticParams.tolerances.iMassToleranceType = 0;
            }
         }
         else if (!strcmp(szParamName, "peptide_mass_units"))
         {
            sscanf(szParamVal, "%d", &g_staticParams.tolerances.iMassToleranceUnits);
            if ((g_staticParams.tolerances.iMassToleranceUnits < 0) || 
                (g_staticParams.tolerances.iMassToleranceUnits > 2))
            {
                g_staticParams.tolerances.iMassToleranceUnits = 0;  // 0=amu, 1=mmu, 2=ppm
            }
            bCurrentParamsFile = 1;
         }
         else if (!strcmp(szParamName, "isotope_error"))
         {
            sscanf(szParamVal, "%d", &g_staticParams.tolerances.iIsotopeError);
            if ((g_staticParams.tolerances.iIsotopeError < 0) || 
                (g_staticParams.tolerances.iIsotopeError > 2))
            {
                g_staticParams.tolerances.iIsotopeError = 0;
            }
         }
         else if (!strcmp(szParamName, "num_output_lines"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.iNumPeptideOutputLines));
         }
         else if (!strcmp(szParamName, "num_results"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.iNumStored));
         }
         else if (!strcmp(szParamName, "remove_precursor_peak"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.iRemovePrecursor));
         }
         else if (!strcmp(szParamName, "remove_precursor_tolerance"))
         {
            sscanf(szParamVal, "%lf", &(g_staticParams.options.dRemovePrecursorTol));
         }
         else if (!strcmp(szParamName, "clear_mz_range"))
         {
            double dStart = 0.0,
                   dEnd = 0.0;

            sscanf(szParamVal, "%lf %lf", &dStart, &dEnd);
            if ((dEnd >= dStart) && (dStart >= 0.0))
            {
               g_staticParams.options.dClearLowMZ = dStart;
               g_staticParams.options.dClearHighMZ = dEnd;
            }
         }
         else if (!strcmp(szParamName, "print_expect_score"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.bPrintExpectScore));
         }
         else if (!strcmp(szParamName, "output_sqtstream"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.bOutputSqtStream));
         }
         else if (!strcmp(szParamName, "output_sqtfile"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.bOutputSqtFile));
         }
         else if (!strcmp(szParamName, "output_txtfile"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.bOutputTxtFile));
         }
         else if (!strcmp(szParamName, "output_pepxmlfile"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.bOutputPepXMLFile));
         }
         else if (!strcmp(szParamName, "output_pinxmlfile"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.bOutputPinXMLFile));
         }
         else if (!strcmp(szParamName, "output_outfiles"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.bOutputOutFiles));
         }
         else if (!strcmp(szParamName, "skip_researching"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.bSkipAlreadyDone));
         }
         else if (!strcmp(szParamName, "variable_C_terminus"))
         {
            sscanf(szParamVal, "%lf", &g_staticParams.variableModParameters.dVarModMassC);
         }
         else if (!strcmp(szParamName, "variable_N_terminus"))
         {
            sscanf(szParamVal, "%lf", &g_staticParams.variableModParameters.dVarModMassN);
         }
         else if (!strcmp(szParamName, "variable_C_terminus_distance"))
         {
            sscanf(szParamVal, "%d", &g_staticParams.variableModParameters.iVarModCtermDistance);
         }
         else if (!strcmp(szParamName, "variable_N_terminus_distance"))
         {
            sscanf(szParamVal, "%d", &g_staticParams.variableModParameters.iVarModNtermDistance);
         }
         else if (!strcmp(szParamName, "add_Cterm_peptide"))
         {
            sscanf(szParamVal, "%lf", &g_staticParams.staticModifications.dAddCterminusPeptide);
         }
         else if (!strcmp(szParamName, "add_Nterm_peptide"))
         {
            sscanf(szParamVal, "%lf", &g_staticParams.staticModifications.dAddNterminusPeptide);
         }
         else if (!strcmp(szParamName, "add_Cterm_protein"))
         {
            sscanf(szParamVal, "%lf", &g_staticParams.staticModifications.dAddCterminusProtein);
         }
         else if (!strcmp(szParamName, "add_Nterm_protein"))
         {
            sscanf(szParamVal, "%lf", &g_staticParams.staticModifications.dAddNterminusProtein);
         }
         else if (!strcmp(szParamName, "add_G_glycine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['G'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_A_alanine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['A'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_S_serine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['S'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_P_proline"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['P'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_V_valine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['V'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_T_threonine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['T'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_C_cysteine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['C'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_L_leucine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['L'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_I_isoleucine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['I'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_N_asparagine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['N'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_O_ornithine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['O'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_D_aspartic_acid"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['D'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_Q_glutamine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['Q'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_K_lysine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['K'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_E_glutamic_acid"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['E'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_M_methionine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['M'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_H_histidine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['H'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_F_phenylalanine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['F'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_R_arginine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['R'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_Y_tyrosine"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['Y'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_W_tryptophan"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['W'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_B_user_amino_acid"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['B'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_J_user_amino_acid"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['J'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_U_user_amino_acid"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['U'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_X_user_amino_acid"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['X'] = dTempMass;
         }
         else if (!strcmp(szParamName, "add_Z_user_amino_acid"))
         {
            sscanf(szParamVal, "%lf", &dTempMass);
            g_staticParams.staticModifications.pdStaticMods['Z'] = dTempMass;
         }
         else if (!strcmp(szParamName, "search_enzyme_number"))
         {
            sscanf(szParamVal, "%d", &iSearchEnzymeNumber);
         }
         else if (!strcmp(szParamName, "sample_enzyme_number"))
         {
            sscanf(szParamVal, "%d", &iSampleEnzymeNumber);
         }
         else if (!strcmp(szParamName, "num_enzyme_termini"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.iEnzymeTermini));
            if ((g_staticParams.options.iEnzymeTermini != 1) && 
                (g_staticParams.options.iEnzymeTermini != 8) && 
                (g_staticParams.options.iEnzymeTermini != 9))
            {
               g_staticParams.options.iEnzymeTermini = 2;
            }
         }
         else if (!strcmp(szParamName, "allowed_missed_cleavage ="))
         {
            sscanf(szParamVal, "%d", &g_staticParams.enzymeInformation.iAllowedMissedCleavage);
            if (g_staticParams.enzymeInformation.iAllowedMissedCleavage < 0)
            {
               g_staticParams.enzymeInformation.iAllowedMissedCleavage = 0;
            }
         }
         else if (!strcmp(szParamName, "scan_range"))
         {
            int iStart=0,
                iEnd=0;

            sscanf(szParamVal, "%d %d", &iStart, &iEnd);
            if ((iEnd >= iStart) && (iStart > 0))
            {
               g_staticParams.options.iStartScan = iStart;
               g_staticParams.options.iEndScan = iEnd;
            }
         }
         else if (!strcmp(szParamName, "spectrum_batch_size"))
         {
            int iSpectrumBatchSize=0;

            sscanf(szParamVal, "%d", &iSpectrumBatchSize);
            if (iSpectrumBatchSize > 0)
            {
               g_staticParams.options.iSpectrumBatchSize = iSpectrumBatchSize;
            }
         }
         else if (!strcmp(szParamName, "minimum_peaks"))
         {
            int iNum = 0;

            sscanf(szParamVal, "%d", &iNum);
            if (iNum > 0)
            {
               g_staticParams.options.iMinPeaks = iNum;
            }
         }
         else if (!strcmp(szParamName, "precursor_charge"))
         {
            int iStart = 0,
                iEnd = 0;

            sscanf(szParamVal, "%d %d", &iStart, &iEnd);
            if ((iEnd >= iStart) && (iStart >= 0) && (iEnd > 0))
            {
               if (iStart==0)
               {
                  g_staticParams.options.iStartCharge = 1;
               }
               else
               {
                  g_staticParams.options.iStartCharge = iStart;
               }

               g_staticParams.options.iEndCharge = iEnd;
            }
         }
         else if (!strcmp(szParamName, "max_fragment_charge"))
         {
            int iCharge = 0;

            sscanf(szParamVal, "%d", &iCharge);
            if (iCharge > MAX_FRAGMENT_CHARGE)
               iCharge = MAX_FRAGMENT_CHARGE;

            if (iCharge > 0)
               g_staticParams.options.iMaxFragmentCharge = iCharge;
            else
               g_staticParams.options.iMaxFragmentCharge = DEFAULT_FRAGMENT_CHARGE;
         }
         else if (!strcmp(szParamName, "max_precursor_charge"))
         {
            int iCharge = 0;

            sscanf(szParamVal, "%d", &iCharge);
            if (iCharge > MAX_PRECURSOR_CHARGE)
               iCharge = MAX_PRECURSOR_CHARGE;

            if (iCharge > 0)
               g_staticParams.options.iMaxPrecursorCharge = iCharge;
            else
               g_staticParams.options.iMaxPrecursorCharge = DEFAULT_PRECURSOR_CHARGE;
         }
         else if (!strcmp(szParamName, "digest_mass_range"))
         {
            double dStart = 0.0,
                   dEnd = 0.0;

            sscanf(szParamVal, "%lf %lf", &dStart, &dEnd);
            if ((dEnd >= dStart) && (dStart >= 0.0))
            {
               g_staticParams.options.dLowPeptideMass = dStart;
               g_staticParams.options.dHighPeptideMass = dEnd;
            }
         }
         else if (!strcmp(szParamName, "ms_level"))
         {
            int iNum = 0;

            sscanf(szParamVal, "%d", &iNum);
            if (iNum == 2)
            {
               g_staticParams.options.iStartMSLevel = 2;
               g_staticParams.options.iEndMSLevel = 0;
            }
            else if (iNum == 3)
            {
               g_staticParams.options.iStartMSLevel = 3;
               g_staticParams.options.iEndMSLevel = 0;
            }
            else
            {
               g_staticParams.options.iStartMSLevel = 2;
               g_staticParams.options.iEndMSLevel = 3;
            }
         }
         else if (!strcmp(szParamName, "activation_method"))
         {
            sscanf(szParamVal, "%24s", g_staticParams.options.szActivationMethod);
         }
         else if (!strcmp(szParamName, "minimum_intensity"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.iMinIntensity));
            if (g_staticParams.options.iMinIntensity < 0)
            {
               g_staticParams.options.iMinIntensity = 0;
            }
         }
         else if (!strcmp(szParamName, "decoy_search"))
         {
            sscanf(szParamVal, "%d", &(g_staticParams.options.iDecoySearch));
            if ((g_staticParams.options.iDecoySearch < 0) || (g_staticParams.options.iDecoySearch > 2))
            {
               g_staticParams.options.iDecoySearch = 0;
            }
         }
      }

   } // while

   if (g_staticParams.tolerances.dFragmentBinSize == 0.0)
      g_staticParams.tolerances.dFragmentBinSize = DEFAULT_BIN_WIDTH;

   // Set dInverseBinWidth to its inverse in order to use a multiply instead of divide in BIN macro.
   g_staticParams.dInverseBinWidth = 1.0 /g_staticParams.tolerances.dFragmentBinSize;
   g_staticParams.dOneMinusBinOffset = 1.0 - g_staticParams.tolerances.dFragmentBinStartOffset;
 
   // Set masses to either average or monoisotopic.
   CometMassSpecUtils::AssignMass(g_staticParams.massUtility.pdAAMassParent, 
                                  g_staticParams.massUtility.bMonoMassesParent, 
                                  &g_staticParams.massUtility.dOH2parent);

   CometMassSpecUtils::AssignMass(g_staticParams.massUtility.pdAAMassFragment, 
                                  g_staticParams.massUtility.bMonoMassesFragment, 
                                  &g_staticParams.massUtility.dOH2fragment); 

   g_staticParams.massUtility.dCO = g_staticParams.massUtility.pdAAMassFragment['c'] 
            + g_staticParams.massUtility.pdAAMassFragment['o'];

   g_staticParams.massUtility.dH2O = g_staticParams.massUtility.pdAAMassFragment['h'] 
            + g_staticParams.massUtility.pdAAMassFragment['h']
            + g_staticParams.massUtility.pdAAMassFragment['o'];

   g_staticParams.massUtility.dNH3 = g_staticParams.massUtility.pdAAMassFragment['n'] 
            + g_staticParams.massUtility.pdAAMassFragment['h'] 
            + g_staticParams.massUtility.pdAAMassFragment['h'] 
            + g_staticParams.massUtility.pdAAMassFragment['h'];

   g_staticParams.massUtility.dNH2 = g_staticParams.massUtility.pdAAMassFragment['n'] 
            + g_staticParams.massUtility.pdAAMassFragment['h'] 
            + g_staticParams.massUtility.pdAAMassFragment['h'];

   g_staticParams.massUtility.dCOminusH2 = g_staticParams.massUtility.dCO
            - g_staticParams.massUtility.pdAAMassFragment['h']
            - g_staticParams.massUtility.pdAAMassFragment['h'];

   fgets(szParamBuf, SIZE_BUF, fp);

   // Get enzyme specificity.
   strcpy(g_staticParams.enzymeInformation.szSearchEnzymeName, "-");
   strcpy(g_staticParams.enzymeInformation.szSampleEnzymeName, "-");
   while (!feof(fp))
   {
      int iCurrentEnzymeNumber;

      sscanf(szParamBuf, "%d.", &iCurrentEnzymeNumber);

      if (iCurrentEnzymeNumber == iSearchEnzymeNumber)
      {
         sscanf(szParamBuf, "%lf %48s %d %20s %20s\n",
               &dTempMass, 
               g_staticParams.enzymeInformation.szSearchEnzymeName, 
               &g_staticParams.enzymeInformation.iSearchEnzymeOffSet, 
               g_staticParams.enzymeInformation.szSearchEnzymeBreakAA, 
               g_staticParams.enzymeInformation.szSearchEnzymeNoBreakAA);
      }

      if (iCurrentEnzymeNumber == iSampleEnzymeNumber)
      {
         sscanf(szParamBuf, "%lf %48s %d %20s %20s\n",
               &dTempMass, 
               g_staticParams.enzymeInformation.szSampleEnzymeName, 
               &g_staticParams.enzymeInformation.iSampleEnzymeOffSet, 
               g_staticParams.enzymeInformation.szSampleEnzymeBreakAA, 
               g_staticParams.enzymeInformation.szSampleEnzymeNoBreakAA);
      }

      fgets(szParamBuf, SIZE_BUF, fp);
   }
   fclose(fp);

   if (!strcmp(g_staticParams.enzymeInformation.szSearchEnzymeName, "-"))
   {
      printf(" Error - search enzyme number %d is missing definition in params file.\n\n", iSearchEnzymeNumber);
      exit(1);
   }
   if (!strcmp(g_staticParams.enzymeInformation.szSampleEnzymeName, "-"))
   {
      printf(" Error - sample enzyme number %d is missing definition in params file.\n\n", iSampleEnzymeNumber);
      exit(1);
   }

   if (!strncmp(g_staticParams.enzymeInformation.szSearchEnzymeBreakAA, "-", 1) && 
       !strncmp(g_staticParams.enzymeInformation.szSearchEnzymeNoBreakAA, "-", 1))
   {
      g_staticParams.options.bNoEnzymeSelected = 1;
   }
   else
   {
      g_staticParams.options.bNoEnzymeSelected = 0;
   }

   // Load ion series to consider, useA, useB, useY are for neutral losses.
   g_staticParams.ionInformation.iNumIonSeriesUsed = 0;
   for (i=0; i<6; i++)
   {
      if (g_staticParams.ionInformation.iIonVal[i] > 0)
         g_staticParams.ionInformation.piSelectedIonSeries[g_staticParams.ionInformation.iNumIonSeriesUsed++] = i;
   }

   // Variable mod search for AAs listed in szVarModChar.
   g_staticParams.szMod[0] = '\0';
   for (i=0; i<VMODS; i++)
   {
      if ((g_staticParams.variableModParameters.varModList[i].dVarModMass != 0.0) &&
          (g_staticParams.variableModParameters.varModList[i].szVarModChar[0]!='\0'))
      {
         sprintf(g_staticParams.szMod + strlen(g_staticParams.szMod), "(%s%c %+0.6f) ", 
               g_staticParams.variableModParameters.varModList[i].szVarModChar,
               g_staticParams.variableModParameters.cModCode[i],
               g_staticParams.variableModParameters.varModList[i].dVarModMass);
         g_staticParams.variableModParameters.bVarModSearch = 1;
      }
   }

   if (g_staticParams.variableModParameters.dVarModMassN != 0.0)
   {
      sprintf(g_staticParams.szMod + strlen(g_staticParams.szMod), "(nt] %+0.6f) ", 
            g_staticParams.variableModParameters.dVarModMassN);       // FIX determine .out file header string for this?
      g_staticParams.variableModParameters.bVarModSearch = 1;
   }
   if (g_staticParams.variableModParameters.dVarModMassC != 0.0)
   {
      sprintf(g_staticParams.szMod + strlen(g_staticParams.szMod), "(ct[ %+0.6f) ", 
            g_staticParams.variableModParameters.dVarModMassC);
      g_staticParams.variableModParameters.bVarModSearch = 1;
   }

   // Do Sp scoring after search based on how many lines to print out.
   if (g_staticParams.options.iNumStored > NUM_STORED)
      g_staticParams.options.iNumStored = NUM_STORED;
   else if (g_staticParams.options.iNumStored < 1)
      g_staticParams.options.iNumStored = 1;


   if (g_staticParams.options.iNumPeptideOutputLines > g_staticParams.options.iNumStored)
      g_staticParams.options.iNumPeptideOutputLines = g_staticParams.options.iNumStored;
   else if (g_staticParams.options.iNumPeptideOutputLines < 1)
      g_staticParams.options.iNumPeptideOutputLines = 1;

   if (g_staticParams.peaksInformation.iNumMatchPeaks > 5)
      g_staticParams.peaksInformation.iNumMatchPeaks = 5;

   // FIX how to deal with term mod on both peptide and protein?
   if (g_staticParams.staticModifications.dAddCterminusPeptide != 0.0)
   {
      sprintf(g_staticParams.szMod + strlen(g_staticParams.szMod), "+ct=%0.6f ", 
            g_staticParams.staticModifications.dAddCterminusPeptide);
   }
   if (g_staticParams.staticModifications.dAddNterminusPeptide != 0.0)
   {
      sprintf(g_staticParams.szMod + strlen(g_staticParams.szMod), "+nt=%0.6f ", 
            g_staticParams.staticModifications.dAddNterminusPeptide);
   }
   if (g_staticParams.staticModifications.dAddCterminusProtein!= 0.0)
   {
      sprintf(g_staticParams.szMod + strlen(g_staticParams.szMod), "+ctprot=%0.6f ", 
            g_staticParams.staticModifications.dAddCterminusProtein);
   }
   if (g_staticParams.staticModifications.dAddNterminusProtein!= 0.0)
   {
      sprintf(g_staticParams.szMod + strlen(g_staticParams.szMod), "+ntprot=%0.6f ", 
            g_staticParams.staticModifications.dAddNterminusProtein);
   }

   for (i=65; i<=90; i++)  // 65-90 represents upper case letters in ASCII
   {
      if (g_staticParams.staticModifications.pdStaticMods[i] != 0.0)
      {
         sprintf(g_staticParams.szMod + strlen(g_staticParams.szMod), "%c=%0.6f ", i,
               g_staticParams.massUtility.pdAAMassParent[i] += g_staticParams.staticModifications.pdStaticMods[i]);
         g_staticParams.massUtility.pdAAMassFragment[i] += g_staticParams.staticModifications.pdStaticMods[i];
      }
      else if (i=='B' || i=='J' || i=='U' || i=='X' || i=='Z')
      {
         g_staticParams.massUtility.pdAAMassParent[i] = 999999.;
         g_staticParams.massUtility.pdAAMassFragment[i] = 999999.;
      }
   }

   // Print out enzyme name to g_staticParams.szMod.
   if (!g_staticParams.options.bNoEnzymeSelected)
   {
      char szTmp[4];

      szTmp[0]='\0';
      if (g_staticParams.options.iEnzymeTermini != 2)
         sprintf(szTmp, ":%d", g_staticParams.options.iEnzymeTermini);

      sprintf(g_staticParams.szMod + strlen(g_staticParams.szMod), "Enzyme:%s (%d%s)", 
            g_staticParams.enzymeInformation.szSearchEnzymeName,
            g_staticParams.enzymeInformation.iAllowedMissedCleavage,
            szTmp);
   }
   else
   {
      sprintf(g_staticParams.szMod + strlen(g_staticParams.szMod), "Enzyme:%s",
            g_staticParams.enzymeInformation.szSearchEnzymeName);
   }

   if (!bCurrentParamsFile)
   {
      fprintf(stderr, " Error - outdated params file; generate an update params file using '-p' option.\n\n");
      exit(1);
   }

   if (g_staticParams.tolerances.dFragmentBinStartOffset < 0.0 || g_staticParams.tolerances.dFragmentBinStartOffset >1.0)
   {
      fprintf(stderr, " Error - bin offset %f must between 0.0 and 1.0\n",
            g_staticParams.tolerances.dFragmentBinStartOffset);
      exit(1);
   }


   // print parameters

   char szIsotope[16];
   char szPeak[16];

   sprintf(g_staticParams.szIonSeries, "ion series ABCXYZ nl: %d%d%d%d%d%d %d",
           g_staticParams.ionInformation.iIonVal[ION_SERIES_A],
           g_staticParams.ionInformation.iIonVal[ION_SERIES_B],
           g_staticParams.ionInformation.iIonVal[ION_SERIES_C],
           g_staticParams.ionInformation.iIonVal[ION_SERIES_X],
           g_staticParams.ionInformation.iIonVal[ION_SERIES_Y],
           g_staticParams.ionInformation.iIonVal[ION_SERIES_Z],
           g_staticParams.ionInformation.bUseNeutralLoss);

   char szUnits[8];
   char szDecoy[20];
   char szReadingFrame[20];
   char szRemovePrecursor[20];

   if (g_staticParams.tolerances.iMassToleranceUnits==0)
      strcpy(szUnits, " AMU");
   else if (g_staticParams.tolerances.iMassToleranceUnits==1)
      strcpy(szUnits, " MMU");
   else
      strcpy(szUnits, " PPM");

   if (g_staticParams.options.iDecoySearch)
      sprintf(szDecoy, " DECOY%d", g_staticParams.options.iDecoySearch);
   else
      szDecoy[0]=0;

   if (g_staticParams.options.iRemovePrecursor)
      sprintf(szRemovePrecursor, " REMOVEPREC%d", g_staticParams.options.iRemovePrecursor);
   else
      szRemovePrecursor[0]=0;

   if (g_staticParams.options.iWhichReadingFrame)
      sprintf(szReadingFrame, " FRAME%d", g_staticParams.options.iWhichReadingFrame);
   else
      szReadingFrame[0]=0;

   szIsotope[0]='\0';
   if (g_staticParams.tolerances.iIsotopeError==1)
      strcpy(szIsotope, "ISOTOPE1");
   else if (g_staticParams.tolerances.iIsotopeError==2)
      strcpy(szIsotope, "ISOTOPE2");

   szPeak[0]='\0';
   if (g_staticParams.ionInformation.iTheoreticalFragmentIons==1)
      strcpy(szPeak, "PEAK1");

   sprintf(g_staticParams.szDisplayLine, "display top %d, %s%s%s%s%s%s%s%s",
         g_staticParams.options.iNumPeptideOutputLines,
         szRemovePrecursor,
         szReadingFrame,
         szPeak,
         szUnits,
         (g_staticParams.tolerances.iMassToleranceType==0?" MH+":" m/z"),
         szIsotope,
         szDecoy,
         (g_staticParams.options.bClipNtermMet?" CLIPMET":"") );

} // LoadParameters


// Parses the command line and determines the type of analysis to perform.
bool ParseCmdLine(char *cmd, InputFileInfo *pInputFile)
{
   char *tok;
   char *scan;

   pInputFile->iAnalysisType = 0;

   // Get the file name. Because Windows can have ":" in the file path,
   // we can't just use "strtok" to grab the filename.
   int i;
   int iCmdLen = strlen(cmd);
   for (i=0; i < iCmdLen; i++)
   {
       if (cmd[i] == ':')
       {
           if ((i + 1) < iCmdLen)
           {
               if (cmd[i+1] != '\\' && cmd[i+1] != '/')
               {
                   break;
               }
           }
       }
   }

   strncpy(pInputFile->szFileName, cmd, i);
   pInputFile->szFileName[i] = '\0';
   if (!ValidateInputMsMsFile(pInputFile->szFileName))
   {
       return false;
   }

   // Get additional filters.
   scan = strtok(cmd+i, ":\n");

   // Analyze entire file.
   if (scan == NULL)
   {
      if (g_staticParams.options.iStartScan == 0 && g_staticParams.options.iEndScan == 0)
      {
         pInputFile->iAnalysisType = AnalysisType_EntireFile;
         return true;
      }
      else
      {
         pInputFile->iAnalysisType = AnalysisType_SpecificScanRange;

         pInputFile->iFirstScan = g_staticParams.options.iStartScan;
         pInputFile->iLastScan = g_staticParams.options.iEndScan;

         if (pInputFile->iFirstScan == 0)  // this means iEndScan is specified only
            pInputFile->iFirstScan = 1;    // so start with 1st scan in file

         // but if iEndScan == 0 then only iStartScan is specified; in this 
         // case search iStartScan through end of file (handled in CometPreprocess)

         return true;
      }
   }

   // Analyze a portion of the file.
   if (strchr(scan,'-') != NULL)
   {
      pInputFile->iAnalysisType = AnalysisType_SpecificScanRange;
      tok = strtok(scan, "-\n");
      if (tok != NULL)
         pInputFile->iFirstScan = atoi(tok);
      tok = strtok(NULL,"-\n");
      if (tok != NULL)
         pInputFile->iLastScan = atoi(tok);
   }
   else if (strchr(scan,'+') != NULL)
   {
      pInputFile->iAnalysisType = AnalysisType_SpecificScanRange;
      tok = strtok(scan,"+\n");
      if (tok != NULL)
         pInputFile->iFirstScan = atoi(tok);
      tok = strtok(NULL,"+\n");
      if (tok != NULL)
         pInputFile->iLastScan = pInputFile->iFirstScan + atoi(tok);
   }
   else
   {
      pInputFile->iAnalysisType = AnalysisType_SpecificScan;
      pInputFile->iFirstScan = atoi(scan);
      pInputFile->iLastScan = pInputFile->iFirstScan;
   }

   return true;
} // ParseCmdLine


void ProcessCmdLine(int argc, 
                    char *argv[], 
                    char *szParamsFile)
{
   bool bPrintParams = false;
   int iStartInputFile = 1;
   char *arg;
   FILE *fpcheck;

   if (iStartInputFile == argc)
   {
      printf("\n");
      printf(" Comet version %s\n %s\n", comet_version, copyright);
      printf("\n");
      printf(" Error - no input files specified so nothing to do.\n\n");
      exit(1);
   }

   strcpy(szParamsFile, "comet.params");

   g_staticParams.databaseInfo.szDatabase[0] = '\0';


   arg = argv[iStartInputFile];

   // First process the command line options; do this only to see if an alternate
   // params file is specified before loading params file first.
   while ((iStartInputFile < argc) && (NULL != arg))
   {
      if (arg[0] == '-')
         SetOptions(arg, szParamsFile, &bPrintParams);

      arg = argv[++iStartInputFile];
   }

   if (bPrintParams)
   {
      PrintParams();
      exit(0);
   }

   // Loads search parameters from comet.params file. This has to happen
   // after parsing command line arguments in case alt file is specified.
   LoadParameters(szParamsFile);

   // Now go through input arguments again.  Command line options will
   // override options specified in params file. 
   iStartInputFile = 1;
   arg = argv[iStartInputFile];
   while ((iStartInputFile < argc) && (NULL != arg))
   {
      if (arg[0] == '-')
      {
         SetOptions(arg, szParamsFile, &bPrintParams);
      }
      else if (arg != NULL)
      {
          InputFileInfo *pInputFileInfo = new InputFileInfo();
          if (!ParseCmdLine(arg, pInputFileInfo))
          {
              fprintf(stderr, " Error - input MS/MS file \"%s\" not found.\n\n", pInputFileInfo->szFileName);
              g_pvInputFiles.clear();
              exit(1);
          }
          g_pvInputFiles.push_back(pInputFileInfo);
      }
      else
      {
         break;
      }

      arg = argv[++iStartInputFile];
   }

   // Quick sanity check to make sure sequence db file is present before spending
   // time reading & processing spectra and then reporting this error.
   if ((fpcheck=fopen(g_staticParams.databaseInfo.szDatabase, "r")) == NULL)
   {
      fprintf(stderr, "\n Error - cannot read database file \"%s\".\n", g_staticParams.databaseInfo.szDatabase);
      fprintf(stderr, " Check that the file exists and is readable.\n\n");
      g_pvInputFiles.clear();
      exit(1);
   }
   fclose(fpcheck);

   if (g_staticParams.options.iEndScan < g_staticParams.options.iStartScan && g_staticParams.options.iEndScan!= 0)
   {
      fprintf(stderr, "\n Comet version %s\n %s\n\n", comet_version, copyright);
      fprintf(stderr, " Error - start scan is %d but end scan is %d.\n", g_staticParams.options.iStartScan, g_staticParams.options.iEndScan);
      fprintf(stderr, " The end scan must be >= to the start scan.\n\n");
      g_pvInputFiles.clear();
      exit(1);
   }

   if (!g_staticParams.options.bOutputOutFiles)
   {
      g_staticParams.options.bSkipAlreadyDone = 0;
   }

   g_staticParams.precalcMasses.dNtermProton = g_staticParams.staticModifications.dAddNterminusPeptide
      + PROTON_MASS;

   g_staticParams.precalcMasses.dCtermOH2Proton = g_staticParams.staticModifications.dAddCterminusPeptide
      + g_staticParams.massUtility.dOH2fragment
      + PROTON_MASS;

   g_staticParams.precalcMasses.dOH2ProtonCtermNterm = g_staticParams.massUtility.dOH2parent
      + PROTON_MASS
      + g_staticParams.staticModifications.dAddCterminusPeptide
      + g_staticParams.staticModifications.dAddNterminusPeptide;
} // ProcessCmdLine


void PrintParams(void)
{
   FILE *fp;

   if ( (fp=fopen("comet.params.new", "w"))==NULL)
   {
      fprintf(stderr, "\n Error - cannot write file comet.params.new\n\n");
      exit(1);
   }

   fprintf(fp, "# comet_version %s\n\
# Comet MS/MS search engine parameters file.\n\
# Everything following the '#' symbol is treated as a comment.\n", comet_version);

   fprintf(fp,
"\n\
database_name = /some/path/db.fasta\n\
decoy_search = 0                       # 0=no (default), 1=concatenated search, 2=separate search\n\
\n\
num_threads = 0                        # 0=poll CPU to set num threads; else specify num threads directly (max %d)\n\
\n", MAX_THREADS);

   fprintf(fp,
"#\n\
# masses\n\
#\n\
peptide_mass_tolerance = 3.00\n\
peptide_mass_units = 0                 # 0=amu, 1=mmu, 2=ppm\n\
mass_type_parent = 1                   # 0=average masses, 1=monoisotopic masses\n\
mass_type_fragment = 1                 # 0=average masses, 1=monoisotopic masses\n\
precursor_tolerance_type = 0           # 0=MH+ (default), 1=precursor m/z\n\
isotope_error = 0                      # 0=off, 1=on -1/0/1/2/3 (standard C13 error), 2= -8/-4/0/4/8 (for +4/+8 labeling)\n\
\n\
#\n\
# search enzyme\n\
#\n\
search_enzyme_number = 1               # choose from list at end of this params file\n\
num_enzyme_termini = 2                 # valid values are 1 (semi-digested), 2 (fully digested, default), 8 N-term, 9 C-term\n\
allowed_missed_cleavage = 2            # maximum value is 5; for enzyme search\n\
\n\
#\n\
# Up to 6 variable modifications are supported\n\
# format:  <mass> <residues> <0=variable/1=binary> <max mods per a peptide>\n\
#     e.g. 79.966331 STY 0 3\n\
#\n\
variable_mod1 = 15.9949 M 0 3\n\
variable_mod2 = 0.0 X 0 3\n\
variable_mod3 = 0.0 X 0 3\n\
variable_mod4 = 0.0 X 0 3\n\
variable_mod5 = 0.0 X 0 3\n\
variable_mod6 = 0.0 X 0 3\n\
max_variable_mods_in_peptide = 5\n\
\n\
#\n\
# fragment ions\n\
#\n\
# ion trap ms/ms:  1.0005 tolerance, 0.4 offset (mono masses), theoretical_fragment_ions = 1\n\
# high res ms/ms:    0.02 tolerance, 0.0 offset (mono masses), theoretical_fragment_ions = 0\n\
#\n\
fragment_bin_tol = 1.0005              # binning to use on fragment ions\n\
fragment_bin_offset = 0.4              # offset position to start the binning (0.0 to 1.0)\n\
theoretical_fragment_ions = 1          # 0=default peak shape, 1=M peak only\n\
use_A_ions = 0\n\
use_B_ions = 1\n\
use_C_ions = 0\n\
use_X_ions = 0\n\
use_Y_ions = 1\n\
use_Z_ions = 0\n\
use_NL_ions = 1                        # 0=no, 1=yes to consider NH3/H2O neutral loss peaks\n\
use_sparse_matrix = 0\n\
\n\
#\n\
# output\n\
#\n\
output_sqtstream = 0                   # 0=no, 1=yes  write sqt to standard output\n\
output_sqtfile = 0                     # 0=no, 1=yes  write sqt file\n\
output_txtfile = 0                     # 0=no, 1=yes  write tab-delimited txt file\n\
output_pepxmlfile = 1                  # 0=no, 1=yes  write pep.xml file\n\
output_pinxmlfile = 0                  # 0=no, 1=yes  write pin.xml file\n\
output_outfiles = 0                    # 0=no, 1=yes  write .out files\n\
print_expect_score = 1                 # 0=no, 1=yes to replace Sp with expect in out & sqt\n\
num_output_lines = 5                   # num peptide results to show\n\
show_fragment_ions = 0                 # 0=no, 1=yes for out files only\n\
\n\
sample_enzyme_number = 1               # Sample enzyme which is possibly different than the one applied to the search.\n\
                                       # Used to calculate NTT & NMC in pepXML output (default=1 for trypsin).\n\
\n\
#\n\
# mzXML parameters\n\
#\n\
scan_range = 0 0                       # start and scan scan range to search; 0 as 1st entry ignores parameter\n\
precursor_charge = 0 0                 # precursor charge range to analyze; does not override mzXML charge; 0 as 1st entry ignores parameter\n\
ms_level = 2                           # MS level to analyze, valid are levels 2 (default) or 3\n\
activation_method = ALL                # activation method; used if activation method set; allowed ALL, CID, ECD, ETD, PQD, HCD, IRMPD\n\
\n\
#\n\
# misc parameters\n\
#\n\
digest_mass_range = 600.0 5000.0       # MH+ peptide mass range to analyze\n\
num_results = 50                       # number of search hits to store internally\n\
skip_researching = 1                   # for '.out' file output only, 0=search everything again (default), 1=don't search if .out exists\n\
max_fragment_charge = %d                # set maximum fragment charge state to analyze (allowed max %d)\n\
max_precursor_charge = %d               # set maximum precursor charge state to analyze (allowed max %d)\n",
      DEFAULT_FRAGMENT_CHARGE,
      MAX_FRAGMENT_CHARGE,
      DEFAULT_PRECURSOR_CHARGE,
      MAX_PRECURSOR_CHARGE);

fprintf(fp,
"nucleotide_reading_frame = 0           # 0=proteinDB, 1-6, 7=forward three, 8=reverse three, 9=all six\n\
clip_nterm_methionine = 0              # 0=leave sequences as-is; 1=also consider sequence w/o N-term methionine\n\
spectrum_batch_size = 0                # max. # of spectra to search at a time; 0 to search the entire scan range in one loop\n\
\n\
#\n\
# spectral processing\n\
#\n\
minimum_peaks = 10                     # minimum num. of peaks in spectrum to search (default %d)\n", MINIMUM_PEAKS);

fprintf(fp,
"minimum_intensity = 0                  # minimum intensity value to read in\n\
remove_precursor_peak = 0              # 0=no, 1=yes, 2=all charge reduced precursor peaks (for ETD)\n\
remove_precursor_tolerance = 1.5       # +- Da tolerance for precursor removal\n\
clear_mz_range = 0.0 0.0               # for iTRAQ/TMT type data; will clear out all peaks in the specified m/z range\n\
\n\
#\n\
# additional modifications\n\
#\n\
\n\
variable_C_terminus = 0.0\n\
variable_N_terminus = 0.0\n\
variable_C_terminus_distance = -1      # -1=all peptides, 0=protein terminus, 1-N = maximum offset from C-terminus\n\
variable_N_terminus_distance = -1      # -1=all peptides, 0=protein terminus, 1-N = maximum offset from N-terminus\n\
\n\
add_Cterm_peptide = 0.0\n\
add_Nterm_peptide = 0.0\n\
add_Cterm_protein = 0.0\n\
add_Nterm_protein = 0.0\n\
\n\
add_G_glycine = 0.0000                 # added to G - avg.  57.0513, mono.  57.02146\n\
add_A_alanine = 0.0000                 # added to A - avg.  71.0779, mono.  71.03711\n\
add_S_serine = 0.0000                  # added to S - avg.  87.0773, mono.  87.03203\n\
add_P_proline = 0.0000                 # added to P - avg.  97.1152, mono.  97.05276\n\
add_V_valine = 0.0000                  # added to V - avg.  99.1311, mono.  99.06841\n\
add_T_threonine = 0.0000               # added to T - avg. 101.1038, mono. 101.04768\n\
add_C_cysteine = 57.021464             # added to C - avg. 103.1429, mono. 103.00918\n\
add_L_leucine = 0.0000                 # added to L - avg. 113.1576, mono. 113.08406\n\
add_I_isoleucine = 0.0000              # added to I - avg. 113.1576, mono. 113.08406\n\
add_N_asparagine = 0.0000              # added to N - avg. 114.1026, mono. 114.04293\n\
add_D_aspartic_acid = 0.0000           # added to D - avg. 115.0874, mono. 115.02694\n\
add_Q_glutamine = 0.0000               # added to Q - avg. 128.1292, mono. 128.05858\n\
add_K_lysine = 0.0000                  # added to K - avg. 128.1723, mono. 128.09496\n\
add_E_glutamic_acid = 0.0000           # added to E - avg. 129.1140, mono. 129.04259\n\
add_M_methionine = 0.0000              # added to M - avg. 131.1961, mono. 131.04048\n\
add_O_ornithine = 0.0000               # added to O - avg. 132.1610, mono  132.08988\n\
add_H_histidine = 0.0000               # added to H - avg. 137.1393, mono. 137.05891\n\
add_F_phenylalanine = 0.0000           # added to F - avg. 147.1739, mono. 147.06841\n\
add_R_arginine = 0.0000                # added to R - avg. 156.1857, mono. 156.10111\n\
add_Y_tyrosine = 0.0000                # added to Y - avg. 163.0633, mono. 163.06333\n\
add_W_tryptophan = 0.0000              # added to W - avg. 186.0793, mono. 186.07931\n\
add_B_user_amino_acid = 0.0000         # added to B - avg.   0.0000, mono.   0.00000\n\
add_J_user_amino_acid = 0.0000         # added to J - avg.   0.0000, mono.   0.00000\n\
add_U_user_amino_acid = 0.0000         # added to U - avg.   0.0000, mono.   0.00000\n\
add_X_user_amino_acid = 0.0000         # added to X - avg.   0.0000, mono.   0.00000\n\
add_Z_user_amino_acid = 0.0000         # added to Z - avg.   0.0000, mono.   0.00000\n\
\n\
#\n\
# COMET_ENZYME_INFO _must_ be at the end of this parameters file\n\
#\n\
[COMET_ENZYME_INFO]\n\
0.  No_enzyme              0      -           -\n\
1.  Trypsin                1      KR          P\n\
2.  Trypsin/P              1      KR          -\n\
3.  Lys_C                  1      K           P\n\
4.  Lys_N                  0      K           -\n\
5.  Arg_C                  1      R           P\n\
6.  Asp_N                  0      D           -\n\
7.  CNBr                   1      M           -\n\
8.  Glu_C                  1      DE          P\n\
9.  PepsinA                1      FL          P\n\
10. Chymotrypsin           1      FWYL        P\n\
\n");

   printf("\n Created:  comet.params.new\n\n");
   fclose(fp);

} // PrintParams


bool ValidateInputMsMsFile(char *pszInputFileName)
{
   FILE *fp;
   if ((fp = fopen(pszInputFileName, "r")) == NULL)
   {
      return false;
   }
   fclose(fp);
   return true;
}
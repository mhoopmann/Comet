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
#include "CometSearch.h"
#include "ThreadPool.h"

#define SEARCHED_WIN_LEN    820000


CometSearch::CometSearch()
{
   // Initialize the header modification string - won't change.

   // Allocate memory for protein sequence if necessary.

   _iSizepcVarModSites = sizeof(char)*MAX_PEPTIDE_LEN_P2;
}


CometSearch::~CometSearch()
{
}


void CometSearch::RunSearch(int minNumThreads,
                            int maxNumThreads)
{
   sDBEntry dbe;
   char szBuf[SIZE_BUF]; 
   FILE *fptr;
   int iTmpCh;
   long lEndPos;
   long lCurrPos;
   bool bTrimDescr;

   // Create the thread pool containing g_staticParams.options.iNumThreads,
   // each hanging around and sleeping until asked to so a search.
   ThreadPool<SearchThreadData *> searchThreadPool(SearchThreadProc, minNumThreads, maxNumThreads);

   g_staticParams.databaseInfo.liTotAACount = 0;
   g_staticParams.databaseInfo.iTotalNumProteins = 0;

   if ((fptr=fopen(g_staticParams.databaseInfo.szDatabase, "rb")) == NULL)
   {
       fprintf(stderr, " Error - cannot read database file \"%s\".\n\n", g_staticParams.databaseInfo.szDatabase);
       exit(1);
   }

   fseek(fptr, 0, SEEK_END);
   lEndPos=ftell(fptr);
   rewind(fptr);

   // Load database entry header.
   iTmpCh = getc(fptr);

   if (!g_staticParams.options.bOutputSqtStream)
   {
      printf(" - Search progress: ");
      fflush(stdout);
   }

   // Loop through entire database.
   while(!feof(fptr))
   {
      // Queue at most 1 additional parameter for threads to process.
      searchThreadPool.WaitForQueuedParams(1);

      dbe.strName = "";
      dbe.strSeq = "";

      // Expect a '>' for sequence header line.
      if (iTmpCh != '>')
      {
         fprintf(stderr, "\n\n Error - database file, expecting definition line here.\n");
         fgets(szBuf, SIZE_BUF, fptr);
         fprintf(stderr, "%c%s", iTmpCh, szBuf);
         fgets(szBuf, SIZE_BUF, fptr);
         fprintf(stderr, "%s", szBuf);
         fgets(szBuf, SIZE_BUF, fptr);
         fprintf(stderr, "%s", szBuf);
         exit(1);
      } 

      bTrimDescr = 0;
      while (((iTmpCh = getc(fptr)) != '\n')
            && (iTmpCh != '\r')
            && (iTmpCh != EOF))
      {
         // Don't bother storing description text past first blank.
         if (isspace(iTmpCh) || iscntrl(iTmpCh))
            bTrimDescr = 1;

         if (!bTrimDescr && dbe.strName.size() < (WIDTH_REFERENCE-1))
            dbe.strName += iTmpCh;
      }

      // Load sequence.
      while (((iTmpCh=getc(fptr)) != '>') && (iTmpCh != EOF))
      {
         if (33<=iTmpCh && iTmpCh<=126) // Ascii physical character range.
         {
            // Convert all sequences to upper case.
            dbe.strSeq += toupper(iTmpCh);
            g_staticParams.databaseInfo.liTotAACount++;
         }
      }

      g_staticParams.databaseInfo.iTotalNumProteins++;

      if (!g_staticParams.options.bOutputSqtStream
            && !(g_staticParams.databaseInfo.iTotalNumProteins%200))
      {
         lCurrPos = ftell(fptr);
         printf("%3d%%", (int)(100.0 * (double)lCurrPos/(double)lEndPos));
         fflush(stdout);
         printf("\b\b\b\b");
      }

      // Now search sequence entry; add threading here so that
      // each protein sequence is passed to a separate thread.
      SearchThreadData *pSearchThreadData = new SearchThreadData(dbe);
      searchThreadPool.Launch(pSearchThreadData);
   }

   // Wait for active search threads to complete processing.
   searchThreadPool.WaitForThreads();

   fclose(fptr);

   if (!g_staticParams.options.bOutputSqtStream)
   {
      printf(" 100%%\n");
   }
}


void CometSearch::SearchThreadProc(SearchThreadData *pSearchThreadData)
{
   CometSearch sqSearch; 
   sqSearch.DoSearch(pSearchThreadData->dbEntry);
   delete pSearchThreadData;
   pSearchThreadData = NULL;
}


void CometSearch::DoSearch(sDBEntry dbe)
{
   // Standard protein database search.
   if (g_staticParams.options.iWhichReadingFrame == 0)
   {
      _proteinInfo.iProteinSeqLength = dbe.strSeq.size();

      SearchForPeptides((char *)dbe.strSeq.c_str(), (char *)dbe.strName.c_str(), 0);

      if (g_staticParams.options.bClipNtermMet && dbe.strSeq[0]=='M')
      {
         _proteinInfo.iProteinSeqLength -= 1;
         SearchForPeptides((char *)dbe.strSeq.c_str()+1, (char *)dbe.strName.c_str(), 1);
      }
   }
   else
   {
      int ii;

      // Nucleotide search; translate NA to AA.

      if ((g_staticParams.options.iWhichReadingFrame == 1) ||
          (g_staticParams.options.iWhichReadingFrame == 2) ||
          (g_staticParams.options.iWhichReadingFrame == 3))
      {
         // Specific forward reading frames.
         ii = g_staticParams.options.iWhichReadingFrame - 1;

         // Creates szProteinSeq[] for each reading frame.
         TranslateNA2AA(&ii, 1,(char *)dbe.strSeq.c_str());
         SearchForPeptides(_proteinInfo.pszProteinSeq, (char *)dbe.strName.c_str(), 0);
      }
      else if ((g_staticParams.options.iWhichReadingFrame == 7) ||
               (g_staticParams.options.iWhichReadingFrame == 9))
      {
         // All 3 forward reading frames.
         for (ii=0; ii<3; ii++)
         {
            TranslateNA2AA(&ii, 1,(char *)dbe.strSeq.c_str());
            SearchForPeptides(_proteinInfo.pszProteinSeq, (char *)dbe.strName.c_str(), 0);
         }
      }

      if ((g_staticParams.options.iWhichReadingFrame == 4) ||
          (g_staticParams.options.iWhichReadingFrame == 5) ||
          (g_staticParams.options.iWhichReadingFrame == 6) ||
          (g_staticParams.options.iWhichReadingFrame == 8) ||
          (g_staticParams.options.iWhichReadingFrame == 9))
      {
         char *pszTemp;
         int seqSize;

         // Generate complimentary strand.
         seqSize = dbe.strSeq.size()+1;
         pszTemp=(char *)malloc(seqSize);
         if (pszTemp == NULL)
         {
            fprintf(stderr, " Error - malloc(szTemp[%d])\n",seqSize);
            exit(1);
         }

         memcpy(pszTemp, (char *)dbe.strSeq.c_str(), seqSize);
         for (ii=0; ii<seqSize; ii++)
         {
            switch (dbe.strSeq[ii])
            {
               case 'G':
                  pszTemp[ii] = 'C';
                  break;
               case 'C':
                  pszTemp[ii] = 'G';
                  break;
               case 'T':
                  pszTemp[ii] = 'A';
                  break;
               case 'A':
                  pszTemp[ii] = 'T';
                  break;
               default:
                  pszTemp[ii] = dbe.strSeq[ii];
               break;
            }
         }

         if ((g_staticParams.options.iWhichReadingFrame == 8) ||
             (g_staticParams.options.iWhichReadingFrame == 9))
         {
            // 3 reading frames on complementary strand.
            for (ii=0; ii<3; ii++)
            {
               TranslateNA2AA(&ii, -1, pszTemp);
               SearchForPeptides(_proteinInfo.pszProteinSeq, (char *)dbe.strName.c_str(), 0);
            }
         }
         else
         {
            // Specific reverse reading frame ... valid values are 4, 5 or 6.
            ii = 6 - g_staticParams.options.iWhichReadingFrame;

            if (ii == 0)
            {
               ii = 2;
            }
            else if (ii == 2)
            {
               ii=0;
            }

            TranslateNA2AA(&ii, -1, pszTemp);
            SearchForPeptides(_proteinInfo.pszProteinSeq, (char *)dbe.strName.c_str(), 0);
         }

         free(pszTemp);
      }
   }
}


// Compare MSMS data to peptide with szProteinSeq from the input database.
void CometSearch::SearchForPeptides(char *szProteinSeq,
                                    char *szProteinName,
                                    bool bNtermPeptideOnly)
{
   int iLenPeptide = 0;
   int iStartPos = 0; 
   int iEndPos = 0;
   int varModCounts[VMODS_ALL];
   int iProteinSeqLengthMinus1 = _proteinInfo.iProteinSeqLength-1;
   int iWhichIonSeries;
   int ctIonSeries;
   int ctLen;
   int ctCharge;
   double dCalcPepMass = 0.0;

   memset(varModCounts, 0, sizeof(varModCounts));

   if (_proteinInfo.iProteinSeqLength > 0)
   {
      dCalcPepMass = g_staticParams.precalcMasses.dOH2ProtonCtermNterm
         + g_staticParams.massUtility.pdAAMassParent[(int)szProteinSeq[0]];

      if (g_staticParams.variableModParameters.bVarModSearch) 
      {
         CountVarMods(varModCounts, szProteinSeq[0]);
         CountBinaryModN(varModCounts, iStartPos);
         CountBinaryModC(varModCounts, iEndPos);
      }
   }

   if (iStartPos == 0)
      dCalcPepMass += g_staticParams.staticModifications.dAddNterminusProtein;
   if (iEndPos == iProteinSeqLengthMinus1)
      dCalcPepMass += g_staticParams.staticModifications.dAddCterminusProtein;

   // Search through entire protein.
   while (iStartPos < _proteinInfo.iProteinSeqLength)
   {
      // Check to see if peptide is within global min/mass range for all queries.
      iLenPeptide = iEndPos-iStartPos+1;

      if (iLenPeptide<MAX_PEPTIDE_LEN)
      {
         int iWhichQuery = WithinMassTolerance(dCalcPepMass, szProteinSeq, iStartPos, iEndPos);

         if (iWhichQuery != -1)
         {
            bool bFirstTimeThroughLoopForPeptide = true;

            // Compare calculated fragment ions against all matching query spectra.
            while (iWhichQuery < (int)g_pvQuery.size())
            {
               if (dCalcPepMass < g_pvQuery.at(iWhichQuery)->_pepMassInfo.dPeptideMassToleranceMinus)
               {
                  // If calculated mass is smaller than low mass range.
                  break;
               }

               // Mass tolerance check for particular query against this candidate peptide mass.
               if (CheckMassMatch(iWhichQuery, dCalcPepMass))
               {
                  char szDecoyProteinName[WIDTH_REFERENCE];
                  char szDecoyPeptide[MAX_PEPTIDE_LEN_P2];  // Allow for prev/next AA in string.

                  // Calculate ion series just once to compare against all relevant query spectra.
                  if (bFirstTimeThroughLoopForPeptide)
                  {
                     bool *pbDuplFragment;
                     int iLenMinus1 = iEndPos - iStartPos; // Equals iLenPeptide minus 1.

                     bFirstTimeThroughLoopForPeptide = false;

                     int i;
                     double dBion = g_staticParams.precalcMasses.dNtermProton;
                     double dYion = g_staticParams.precalcMasses.dCtermOH2Proton;
            
                     if (iStartPos == 0)
                        dBion += g_staticParams.staticModifications.dAddNterminusProtein;
                     if (iEndPos == iProteinSeqLengthMinus1)
                        dYion += g_staticParams.staticModifications.dAddCterminusProtein;

                     int iPos;
                     for (i=iStartPos; i<iEndPos; i++)
                     {
                        iPos = i-iStartPos;

                        dBion += g_staticParams.massUtility.pdAAMassFragment[(int)szProteinSeq[i]];
                        _pdAAforward[iPos] = dBion;
            
                        dYion += g_staticParams.massUtility.pdAAMassFragment[(int)szProteinSeq[iEndPos-iPos]];
                        _pdAAreverse[iPos] = dYion;
                     }

                     // Now get the set of binned fragment ions once to compare this peptide against all matching spectra.
                     if ((pbDuplFragment = (bool*)malloc(g_pvQuery.at(iWhichQuery)->_spectrumInfoInternal.iArraySize * (size_t)sizeof(bool)))==NULL)
                     {
                        fprintf(stderr, " Error - malloc pbDuplFragments; iWhichQuery = %d\n\n", iWhichQuery);
                        exit(1);
                     }

                     for (ctIonSeries=0; ctIonSeries<g_staticParams.ionInformation.iNumIonSeriesUsed; ctIonSeries++)
                     {
                        iWhichIonSeries = g_staticParams.ionInformation.piSelectedIonSeries[ctIonSeries];
                        for (ctCharge=1; ctCharge<=g_massRange.iMaxFragmentCharge; ctCharge++)
                        {
                           for (ctLen=0; ctLen<iLenMinus1; ctLen++)
                              pbDuplFragment[BIN(GetFragmentIonMass(iWhichIonSeries, ctLen, ctCharge, _pdAAforward, _pdAAreverse))] = false;
                        }
                     }

                     for (ctIonSeries=0; ctIonSeries<g_staticParams.ionInformation.iNumIonSeriesUsed; ctIonSeries++)
                     {
                        iWhichIonSeries = g_staticParams.ionInformation.piSelectedIonSeries[ctIonSeries];

                        for (ctCharge=1; ctCharge<=g_massRange.iMaxFragmentCharge; ctCharge++)
                        {
                           // As both _pdAAforward and _pdAAreverse are increasing, loop through
                           // iLenPeptide-1 to complete set of internal fragment ions.
                           for (ctLen=0; ctLen<iLenMinus1; ctLen++)
                           {
                              int iVal = BIN(GetFragmentIonMass(iWhichIonSeries, ctLen, ctCharge, _pdAAforward, _pdAAreverse));

                              if (pbDuplFragment[iVal] == false)
                              {
                                 _uiBinnedIonMasses[ctCharge][ctIonSeries][ctLen] = iVal;
                                 pbDuplFragment[iVal] = true;
                              }
                              else
                                 _uiBinnedIonMasses[ctCharge][ctIonSeries][ctLen] = 0;
                           }
                        }
                     }

                     // Also take care of decoy here.
                     if (g_staticParams.options.iDecoySearch)
                     {
#ifdef _WIN32
                        _snprintf(szDecoyProteinName, WIDTH_REFERENCE, "DECOY_%s", szProteinName);
                        szDecoyProteinName[WIDTH_REFERENCE-1]=0;  // _snprintf does not guarantee null termination
#else
                        snprintf(szDecoyProteinName, WIDTH_REFERENCE, "DECOY_%s", szProteinName);
#endif
                        // Generate reverse peptide.  Keep prev and next AA in szDecoyPeptide string.
                        // So actual reverse peptide starts at position 1 and ends at len-2 (as len-1
                        // is next AA).
    
                        // Store flanking residues from original sequence.
                        if (iStartPos==0)
                           szDecoyPeptide[0]='-';
                        else
                           szDecoyPeptide[0]=szProteinSeq[iStartPos-1];

                        if (iEndPos == iProteinSeqLengthMinus1)
                           szDecoyPeptide[iLenPeptide+1]='-';
                        else
                           szDecoyPeptide[iLenPeptide+1]=szProteinSeq[iEndPos+1];
                        szDecoyPeptide[iLenPeptide+2]='\0';

                        if (g_staticParams.enzymeInformation.iSearchEnzymeOffSet==1)
                        {
                           // Last residue stays the same:  change ABCDEK to EDCBAK.
                           for (i=iEndPos-1; i>=iStartPos; i--)
                              szDecoyPeptide[iEndPos-i] = szProteinSeq[i];

                           szDecoyPeptide[iEndPos-iStartPos+1]=szProteinSeq[iEndPos];  // Last residue stays same.
                        }
                        else
                        {
                           // First residue stays the same:  change ABCDEK to AKEDCB.
                           for (i=iEndPos; i>=iStartPos+1; i--)
                              szDecoyPeptide[iEndPos-i+2] = szProteinSeq[i];

                           szDecoyPeptide[1]=szProteinSeq[iStartPos];  // First residue stays same.
                        }

                        // Now given szDecoyPeptide, calculate pdAAforwardDecoy and pdAAreverseDecoy.
                        dBion = g_staticParams.precalcMasses.dNtermProton;
                        dYion = g_staticParams.precalcMasses.dCtermOH2Proton;
            
                        if (iStartPos == 0)
                           dBion += g_staticParams.staticModifications.dAddNterminusProtein;
                        if (iEndPos == iProteinSeqLengthMinus1)
                           dYion += g_staticParams.staticModifications.dAddCterminusProtein;
            
                        for (i=1; i<iLenMinus1; i++)
                        {
                           dBion += g_staticParams.massUtility.pdAAMassFragment[(int)szDecoyPeptide[i]];
                           _pdAAforwardDecoy[i] = dBion;
            
                           dYion += g_staticParams.massUtility.pdAAMassFragment[(int)szDecoyPeptide[iLenMinus1-i]];
                           _pdAAreverseDecoy[i] = dYion;
                        }

                        for (ctIonSeries=0; ctIonSeries<g_staticParams.ionInformation.iNumIonSeriesUsed; ctIonSeries++)
                        {
                           iWhichIonSeries = g_staticParams.ionInformation.piSelectedIonSeries[ctIonSeries];
                           for (ctCharge=1; ctCharge<=g_massRange.iMaxFragmentCharge; ctCharge++)
                           {
                              for (ctLen=0; ctLen<iLenMinus1; ctLen++)
                              {
                                 pbDuplFragment[BIN(GetFragmentIonMass(iWhichIonSeries, ctLen, ctCharge, _pdAAforwardDecoy, _pdAAreverseDecoy))] = false;
                              }
                           }
                        }

                        // Now get the set of binned fragment ions once to compare this peptide against all matching spectra.
                        for (ctIonSeries=0; ctIonSeries<g_staticParams.ionInformation.iNumIonSeriesUsed; ctIonSeries++)
                        {
                           iWhichIonSeries = g_staticParams.ionInformation.piSelectedIonSeries[ctIonSeries];

                           for (ctCharge=1; ctCharge<=g_massRange.iMaxFragmentCharge; ctCharge++)
                           {
                              // As both _pdAAforward and _pdAAreverse are increasing, loop through
                              // iLenPeptide-1 to complete set of internal fragment ions.
                              for (ctLen=0; ctLen<iLenMinus1; ctLen++)
                              {
                                 int iVal = BIN(GetFragmentIonMass(iWhichIonSeries, ctLen, ctCharge, _pdAAforwardDecoy, _pdAAreverseDecoy));

                                 if (pbDuplFragment[iVal] == false)
                                 {
                                    _uiBinnedIonMassesDecoy[ctCharge][ctIonSeries][ctLen] = iVal;
                                    pbDuplFragment[iVal] = true;
                                 }
                                 else
                                    _uiBinnedIonMassesDecoy[ctCharge][ctIonSeries][ctLen] = 0;
                              }
                           }
                        }
                     }

                     free(pbDuplFragment);
                  }

                  char pcVarModSites[4]; // This is unused variable mod placeholder to pass into XcorrScore.

                  XcorrScore(szProteinSeq, szProteinName, iStartPos, iEndPos, false,
                        dCalcPepMass, false, iWhichQuery, iLenPeptide, pcVarModSites);

                  if (g_staticParams.options.iDecoySearch)
                  {
                     XcorrScore(szDecoyPeptide, szDecoyProteinName, 1, iLenPeptide, false,
                           dCalcPepMass, true, iWhichQuery, iLenPeptide, pcVarModSites);
                  }

               }
               iWhichQuery++;
            }
         }
      }

      // Increment end.
      if (dCalcPepMass <= g_massRange.dMaxMass && iEndPos < iProteinSeqLengthMinus1 && iLenPeptide<MAX_PEPTIDE_LEN)
      {
         iEndPos++;
      
         if (g_staticParams.variableModParameters.bVarModSearch) 
            CountBinaryModC(&varModCounts[0], iEndPos);

         if (iEndPos < _proteinInfo.iProteinSeqLength)
         {
            dCalcPepMass += (double)g_staticParams.massUtility.pdAAMassParent[(int)szProteinSeq[iEndPos]];

            if (g_staticParams.variableModParameters.bVarModSearch) 
               CountVarMods(varModCounts, szProteinSeq[iEndPos]);

            if (iEndPos == iProteinSeqLengthMinus1)
               dCalcPepMass += g_staticParams.staticModifications.dAddCterminusProtein;
         }
      } 

      // Increment start, reset end.
      else if (dCalcPepMass > g_massRange.dMaxMass || iEndPos==iProteinSeqLengthMinus1 || iLenPeptide == MAX_PEPTIDE_LEN)
      {
         // Run variable  mod search with each new iStartPos.
         if (g_staticParams.variableModParameters.bVarModSearch)
         {
            // If any variable mod mass is negative, consider adding to iEndPos as long
            // as peptide minus all possible negative mods is less than the dMaxMass????
            //
            // Otherwise, at this point, peptide mass is too big which means should be ok for varmod search.

            if (TotalVarModCount(varModCounts, varModCounts[VMOD_C_INDEX], varModCounts[VMOD_N_INDEX]) > 0)
               VarModSearch(szProteinSeq, szProteinName, varModCounts, iStartPos, iEndPos);

            SubtractVarMods(varModCounts, szProteinSeq[iStartPos]);
         }

         if (bNtermPeptideOnly)
            return;

         dCalcPepMass -= (double)g_staticParams.massUtility.pdAAMassParent[(int)szProteinSeq[iStartPos]];
         if (iStartPos == 0)
            dCalcPepMass -= g_staticParams.staticModifications.dAddNterminusProtein;
         iStartPos++;          // Increment start of peptide.

         if (g_staticParams.variableModParameters.bVarModSearch) 
            CountBinaryModN(varModCounts, iStartPos);

         // Peptide is still potentially larger than input mass so need to delete AA from the end.
         while (dCalcPepMass >= g_massRange.dMinMass
               && iEndPos > iStartPos)
         {
            dCalcPepMass -= (double)g_staticParams.massUtility.pdAAMassParent[(int)szProteinSeq[iEndPos]];
            SubtractVarMods(varModCounts, szProteinSeq[iEndPos]);

            if (iEndPos == iProteinSeqLengthMinus1)
               dCalcPepMass -= g_staticParams.staticModifications.dAddCterminusProtein;
            iEndPos--;
         }
      }
   }
}


int CometSearch::WithinMassTolerance(double dCalcPepMass,
                                     char* szProteinSeq,
                                     int iStartPos,
                                     int iEndPos)
{
   if (dCalcPepMass >= g_massRange.dMinMass
         && dCalcPepMass <= g_massRange.dMaxMass
         && CheckEnzymeTermini(szProteinSeq, iStartPos, iEndPos))
   {
      // Now that we know it's within the global mass range of our queries and has
      // proper enzyme termini, check if within mass tolerance of any given entry.

      int iPos;

      // Do a binary search on list of input queries to find matching mass.
      iPos=BinarySearchMass(0, g_pvQuery.size(), dCalcPepMass);

      // Seek back to first peptide entry that matches mass tolerance in case binary
      // search doesn't hit the first entry.
      while (iPos>0 && g_pvQuery.at(iPos)->_pepMassInfo.dPeptideMassTolerancePlus >= dCalcPepMass)
         iPos--;

      if (iPos != -1)
         return iPos;
      else
         return -1;
   }
   else
   {
      return -1;
   }
}


// Check enzyme termini.
bool CometSearch::CheckEnzymeTermini(char *szProteinSeq,
                                     int iStartPos,
                                     int iEndPos)
{
   if (!g_staticParams.options.bNoEnzymeSelected)
   {
      bool bBeginCleavage=0;
      bool bEndCleavage=0;
      bool bBreakPoint;
      int iOneMinusEnzymeOffSet = 1 - g_staticParams.enzymeInformation.iSearchEnzymeOffSet;
      int iTwoMinusEnzymeOffSet = 2 - g_staticParams.enzymeInformation.iSearchEnzymeOffSet;
      int iCountInternalCleavageSites=0;

      bBeginCleavage = (iStartPos==0
            || szProteinSeq[iStartPos-1]=='*'
            || (strchr(g_staticParams.enzymeInformation.szSearchEnzymeBreakAA, szProteinSeq[iStartPos -1 + iOneMinusEnzymeOffSet])
          && !strchr(g_staticParams.enzymeInformation.szSearchEnzymeNoBreakAA, szProteinSeq[iStartPos -1 + iTwoMinusEnzymeOffSet])));

      bEndCleavage = (iEndPos==(int)(_proteinInfo.iProteinSeqLength-1)
            || szProteinSeq[iEndPos+1]=='*'
            || (strchr(g_staticParams.enzymeInformation.szSearchEnzymeBreakAA, szProteinSeq[iEndPos + iOneMinusEnzymeOffSet])
          && !strchr(g_staticParams.enzymeInformation.szSearchEnzymeNoBreakAA, szProteinSeq[iEndPos + iTwoMinusEnzymeOffSet])));

      // Check full enzyme search.
      if ((g_staticParams.options.iEnzymeTermini == ENZYME_DOUBLE_TERMINI) && !(bBeginCleavage && bEndCleavage))
         return false;

      // Check semi enzyme search.
      if ((g_staticParams.options.iEnzymeTermini == ENZYME_SINGLE_TERMINI) && !(bBeginCleavage || bEndCleavage))
         return false;

      // Check single n-termini enzyme.
      if ((g_staticParams.options.iEnzymeTermini == ENZYME_N_TERMINI) && !bBeginCleavage)
         return false;

      // Check single c-termini enzyme.
      if ((g_staticParams.options.iEnzymeTermini == ENZYME_C_TERMINI) && !bEndCleavage)
         return false;

      // Check number of missed cleavages count.
      int i;
      for (i=iStartPos; i<=iEndPos; i++)
      {
         bBreakPoint = strchr(g_staticParams.enzymeInformation.szSearchEnzymeBreakAA, szProteinSeq[i+iOneMinusEnzymeOffSet])
            && !strchr(g_staticParams.enzymeInformation.szSearchEnzymeNoBreakAA, szProteinSeq[i+iTwoMinusEnzymeOffSet]);

         if (bBreakPoint)
         {
            if ((iOneMinusEnzymeOffSet == 0 && i!=iEndPos)  // Ignore last residue.
                  || (iOneMinusEnzymeOffSet == 1 && i!= iStartPos))  // Ignore first residue.
            {
               iCountInternalCleavageSites++;

               // Need to include -iOneMinusEnzymeOffSet in if statement below because for
               // AspN cleavage, the very last residue, if followed by a D, will be counted
               // as an internal cleavage site.
               if (iCountInternalCleavageSites-iOneMinusEnzymeOffSet > g_staticParams.enzymeInformation.iAllowedMissedCleavage)
                  return false;
            }
         }
      }
   }

   return true;
}


int CometSearch::BinarySearchMass(int start,
                                  int end,
                                  double dCalcPepMass)
{
   // Termination condition: start index greater than end index.
   if(start > end)
      return -1;

   // Find the middle element of the vector and use that for splitting
   // the array into two pieces.
   unsigned middle = start + ((end - start) / 2);

   if (g_pvQuery.at(middle)->_pepMassInfo.dPeptideMassToleranceMinus <= dCalcPepMass
         && dCalcPepMass <= g_pvQuery.at(middle)->_pepMassInfo.dPeptideMassTolerancePlus)
   {
      return middle;
   }
   else if(g_pvQuery.at(middle)->_pepMassInfo.dPeptideMassToleranceMinus > dCalcPepMass)
   {
      return BinarySearchMass(start, middle - 1, dCalcPepMass);
   }
 
   return BinarySearchMass(middle + 1, end, dCalcPepMass);
}


bool CometSearch::CheckMassMatch(int iWhichQuery,
                                 double dCalcPepMass)
{
   Query* pQuery = g_pvQuery.at(iWhichQuery);

   if ((dCalcPepMass >= pQuery->_pepMassInfo.dPeptideMassToleranceMinus)
         && (dCalcPepMass <= pQuery->_pepMassInfo.dPeptideMassTolerancePlus))
   {
      if (g_staticParams.tolerances.iIsotopeError == 0)
      {
         return true;
      }
      else if (g_staticParams.tolerances.iIsotopeError == 1)
      {
         double dC13diff = 1.00335483;
         double dTwoC13diff = 1.00335483 + 1.00335483;
         double dThreeC13diff = 1.00335483 + 1.00335483 + 1.00335483;

         // Using C13 isotope mass difference here but likely should
         // be slightly bigger for other elemental contaminents.

         if ((fabs(pQuery->_pepMassInfo.dExpPepMass - dCalcPepMass)
                  <= pQuery->_pepMassInfo.dPeptideMassTolerance)
               || (fabs(pQuery->_pepMassInfo.dExpPepMass - dCalcPepMass - dC13diff)
                  <= pQuery->_pepMassInfo.dPeptideMassTolerance)
               || (fabs(pQuery->_pepMassInfo.dExpPepMass - dCalcPepMass - dTwoC13diff)
                  <= pQuery->_pepMassInfo.dPeptideMassTolerance)
               || (fabs(pQuery->_pepMassInfo.dExpPepMass - dCalcPepMass - dThreeC13diff)
                  <= pQuery->_pepMassInfo.dPeptideMassTolerance)
               || (fabs(pQuery->_pepMassInfo.dExpPepMass - dCalcPepMass + dC13diff)
                  <= pQuery->_pepMassInfo.dPeptideMassTolerance))
         {
            return true;
         }            
         else
         {
            return false;
         }
      }
      else if (g_staticParams.tolerances.iIsotopeError == 2)
      {
         if ((fabs(pQuery->_pepMassInfo.dExpPepMass - dCalcPepMass)
                  <= pQuery->_pepMassInfo.dPeptideMassTolerance)
               || (fabs(pQuery->_pepMassInfo.dExpPepMass - dCalcPepMass - 4.0070995)
                  <= pQuery->_pepMassInfo.dPeptideMassTolerance)
               || (fabs(pQuery->_pepMassInfo.dExpPepMass - dCalcPepMass - 8.014199)
                  <= pQuery->_pepMassInfo.dPeptideMassTolerance)
               || (fabs(pQuery->_pepMassInfo.dExpPepMass - dCalcPepMass + 4.0070995)
                  <= pQuery->_pepMassInfo.dPeptideMassTolerance)
               || (fabs(pQuery->_pepMassInfo.dExpPepMass - dCalcPepMass + 8.014199)
                  <= pQuery->_pepMassInfo.dPeptideMassTolerance))
         {
            return true;
         }            
         else
         {
            return false;
         }
      }
      else
      {
         fprintf(stderr, " Error - iIsotopeError=%d, should not be here!\n\n", g_staticParams.tolerances.iIsotopeError);
         exit(1);
      }
   }
   else
   {
      return false;
   }
}


// For nucleotide search, translate from DNA to amino acid.
void CometSearch::TranslateNA2AA(int *frame,
                                 int iDirection,
                                 char *szDNASequence)
{
   int i, ii=0;
   int iSeqLength = strlen(szDNASequence);

   if (iDirection == 1)  // Forward reading frame.
   {
      i = (*frame);
      while ((i+2) < iSeqLength)
      {
         if (ii >= _proteinInfo.iAllocatedProtSeqLength)
         {
            char *pTmp;

            pTmp=(char *)realloc(_proteinInfo.pszProteinSeq, ii+100);
            if (pTmp == NULL)
            {
               fprintf(stderr, " Error realloc(szProteinSeq) ... size=%d\n", ii);
               fprintf(stderr, " A sequence entry is larger than your system can handle.\n");
               fprintf(stderr, " Either add more memory or edit the database and divide\n");
               fprintf(stderr, " the sequence into multiple, overlapping, smaller entries.\n\n");
               exit(1);
            }

            _proteinInfo.pszProteinSeq = pTmp;
            _proteinInfo.iAllocatedProtSeqLength=ii+99;
         }

         *(_proteinInfo.pszProteinSeq+ii) = GetAA(i, 1, szDNASequence);
         i += 3;
         ii++;
      }
      _proteinInfo.iProteinSeqLength = ii;
      _proteinInfo.pszProteinSeq[ii] = '\0';
   }
   else                 // Reverse reading frame.
   {
      i = iSeqLength - (*frame) - 1;
      while (i >= 2)    // positions 2,1,0 makes the last AA
      {
         if (ii >= _proteinInfo.iAllocatedProtSeqLength)
         {
            char *pTmp;

            pTmp=(char *)realloc(_proteinInfo.pszProteinSeq, ii+100);
            if (pTmp == NULL)
            {
               fprintf(stderr, " Error realloc(szProteinSeq) ... size=%d\n", ii);
               fprintf(stderr, " A sequence entry is larger than your system can handle.\n");
               fprintf(stderr, " Either add more memory or edit the database and divide\n");
               fprintf(stderr, " the sequence into multiple, overlapping, smaller entries.\n\n");
               exit(1);
            }

            _proteinInfo.pszProteinSeq = pTmp;
            _proteinInfo.iAllocatedProtSeqLength = ii+99;
         }

         *(_proteinInfo.pszProteinSeq + ii) = GetAA(i, -1, szDNASequence);
         i -= 3;
         ii++;
      }
      _proteinInfo.iProteinSeqLength = ii;
      _proteinInfo.pszProteinSeq[ii]='\0';
   }
}


// GET amino acid from DNA triplets, direction=+/-1.
char CometSearch::GetAA(int i,
                        int iDirection,
                        char *szDNASequence)
{
   int iBase1 = i;
   int iBase2 = i + iDirection;
   int iBase3 = i + iDirection*2;

   if (szDNASequence[iBase1]=='G')
   {
      if (szDNASequence[iBase2]=='T')
         return ('V');
      else if (szDNASequence[iBase2]=='C')
         return ('A');
      else if (szDNASequence[iBase2]=='G')
         return ('G');
      else if (szDNASequence[iBase2]=='A')
      {
         if (szDNASequence[iBase3]=='T' || szDNASequence[iBase3]=='C')
            return ('D');
         else if (szDNASequence[iBase3]=='A' || szDNASequence[iBase3]=='G')
            return ('E');
      }
   }

   else if (szDNASequence[iBase1]=='C')
   {
      if (szDNASequence[iBase2]=='T')
         return ('L');
      else if (szDNASequence[iBase2]=='C')
         return ('P');
      else if (szDNASequence[iBase2]=='G')
         return ('R');
      else if (szDNASequence[iBase2]=='A')
      {
         if (szDNASequence[iBase3]=='T' || szDNASequence[iBase3]=='C')
            return ('H');
         else if (szDNASequence[iBase3]=='A' || szDNASequence[iBase3]=='G')
            return ('Q');
      }
   }

   else if (szDNASequence[iBase1]=='T')
   {
      if (szDNASequence[iBase2]=='C')
         return ('S');
      else if (szDNASequence[iBase2]=='T')
      {
         if (szDNASequence[iBase3]=='T' || szDNASequence[iBase3]=='C')
            return ('F');
         else if (szDNASequence[iBase3]=='A' || szDNASequence[iBase3]=='G')
            return ('L');
      }
      else if (szDNASequence[iBase2]=='A')
      {
         if (szDNASequence[iBase3]=='T' || szDNASequence[iBase3]=='C')
            return ('Y');
         else if (szDNASequence[iBase3]=='A' || szDNASequence[iBase3]=='G')
            return ('@');
      }
      else if (szDNASequence[iBase2]=='G')
      {
         if (szDNASequence[iBase3]=='T' || szDNASequence[iBase3]=='C')
            return ('C');
         else if (szDNASequence[iBase3]=='A')
            return ('@');
         else if (szDNASequence[iBase3]=='G')
            return ('W');
      }
   }

   else if (szDNASequence[iBase1]=='A')
   {
      if (szDNASequence[iBase2]=='C')
         return ('T');
      else if (szDNASequence[iBase2]=='T')
      {
         if (szDNASequence[iBase3]=='T' || szDNASequence[iBase3]=='C' || szDNASequence[iBase3]=='A')
            return ('I');
         else if (szDNASequence[iBase3]=='G')
            return ('M');
      }
      else if (szDNASequence[iBase2]=='A')
      {
         if (szDNASequence[iBase3]=='T' || szDNASequence[iBase3]=='C')
            return ('N');
         else if (szDNASequence[iBase3]=='A' || szDNASequence[iBase3]=='G')
            return ('K');
      }
      else if (szDNASequence[iBase2]=='G')
      {
         if (szDNASequence[iBase3]=='T' || szDNASequence[iBase3]=='C')
            return ('S');
         else if (szDNASequence[iBase3]=='A' || szDNASequence[iBase3]=='G')
            return ('R');
      }
   }

   return ('*');

}


// Compares sequence to MSMS spectrum by matching ion intensities.
void CometSearch::XcorrScore(char *szProteinSeq,
                             char *szProteinName,
                             int iStartPos,
                             int iEndPos,
                             bool bFoundVariableMod,
                             double dCalcPepMass,
                             bool bDecoyPep,
                             int iWhichQuery,
                             int iLenPeptide,
                             char *pcVarModSites)
{
   int  ctLen,
        ctIonSeries,
        ctCharge;
   double dXcorr;
   int iLenPeptideMinus1 = iLenPeptide - 1;

   // Pointer to either regular or decoy uiBinnedIonMasses[][][].
   unsigned int (*p_uiBinnedIonMasses)[MAX_FRAGMENT_CHARGE+1][9][MAX_PEPTIDE_LEN];

   // Point to right set of arrays depending on target or decoy search.
   if (bDecoyPep)
      p_uiBinnedIonMasses = &_uiBinnedIonMassesDecoy;
   else
      p_uiBinnedIonMasses = &_uiBinnedIonMasses;

   int iWhichIonSeries;
   bool bUseNLPeaks = false;
   Query* pQuery = g_pvQuery.at(iWhichQuery);

   struct SparseMatrix *pSparseFastXcorrData;  // use this if bSparseMatrix
   float *pFastXcorrData;                      // use this if not using SparseMatrix

   dXcorr = 0.0;

   for (ctIonSeries=0; ctIonSeries<g_staticParams.ionInformation.iNumIonSeriesUsed; ctIonSeries++)
   {
      iWhichIonSeries = g_staticParams.ionInformation.piSelectedIonSeries[ctIonSeries];

      if (g_staticParams.ionInformation.bUseNeutralLoss && (iWhichIonSeries==0 || iWhichIonSeries==1 || iWhichIonSeries==7))
         bUseNLPeaks = true;
      else
         bUseNLPeaks = false;

      for (ctCharge=1; ctCharge<=pQuery->_spectrumInfoInternal.iMaxFragCharge; ctCharge++)
      {
         if (ctCharge == 1 && bUseNLPeaks)
         {
            pSparseFastXcorrData = pQuery->pSparseFastXcorrDataNL;
            pFastXcorrData = pQuery->pfFastXcorrDataNL;
         }
         else
         {
            pSparseFastXcorrData = pQuery->pSparseFastXcorrData;
            pFastXcorrData = pQuery->pfFastXcorrData;
         }

         if (g_staticParams.options.bSparseMatrix)
         {
            //MH: ratchet through pfFastXcorrData
            //This assumes fragment ions are in order...
            int xx=0;
            for (ctLen=0; ctLen<iLenPeptideMinus1; ctLen++)
            {
               if(*(*(*(*p_uiBinnedIonMasses + ctCharge)+ctIonSeries)+ctLen)==0)
                  continue;
               while( *(*(*(*p_uiBinnedIonMasses + ctCharge)+ctIonSeries)+ctLen) >=  (unsigned)pSparseFastXcorrData[xx].bin)
                  xx++;
               dXcorr += pSparseFastXcorrData[xx-1].fIntensity;
            }
         }
         else
         {
            for (ctLen=0; ctLen<iLenPeptideMinus1; ctLen++)
               dXcorr += pFastXcorrData[ *(*(*(*p_uiBinnedIonMasses + ctCharge)+ctIonSeries)+ctLen) ];

            // *(*(*(*p_uiBinnedIonMasses + ctCharge)+ctIonSeries)+ctLen) gives uiBinnedIonMasses[ctCharge][ctIonSeries][ctLen].
         }
      }
   }

   if (dXcorr <= 0.0)
      dXcorr = 0.0;
   else
      dXcorr *= 0.005;  // Scale intensities to 50 and divide score by 1E5.

   Threading::LockMutex(pQuery->accessMutex);

   // Increment matched peptide counts.
   if (bDecoyPep && g_staticParams.options.iDecoySearch == 2)
      pQuery->_liNumMatchedDecoyPeptides++;
   else
      pQuery->_liNumMatchedPeptides++;

   if (g_staticParams.options.bPrintExpectScore)
   {
      int iTmp;

      iTmp = (int)(dXcorr * 10.0 + 0.5);
      if (iTmp >= HISTO_SIZE)
         iTmp = HISTO_SIZE - 1;
   
      if (bDecoyPep && g_staticParams.options.iDecoySearch==2)
         pQuery->iDecoyCorrelationHistogram[iTmp] += 1;
      else
         pQuery->iCorrelationHistogram[iTmp] += 1;
   }

   if (bDecoyPep && g_staticParams.options.iDecoySearch==2)
   {
      if (dXcorr > pQuery->fLowestDecoyCorrScore)
      {
         if (!CheckDuplicate(iWhichQuery, iStartPos, iEndPos, bFoundVariableMod, dCalcPepMass,
                  szProteinSeq, szProteinName, 1, pcVarModSites))
         {
            StorePeptide(iWhichQuery, iStartPos, iEndPos, bFoundVariableMod, szProteinSeq,
                  dCalcPepMass, dXcorr, szProteinName, 1,  pcVarModSites);
         }
      }
   }
   else
   {
      if (dXcorr > pQuery->fLowestCorrScore)
      {
         if (!CheckDuplicate(iWhichQuery, iStartPos, iEndPos, bFoundVariableMod, dCalcPepMass,
                  szProteinSeq, szProteinName, 0, pcVarModSites))
         {
            StorePeptide(iWhichQuery, iStartPos, iEndPos, bFoundVariableMod, szProteinSeq,
                  dCalcPepMass, dXcorr, szProteinName, 0, pcVarModSites);
         }
      }      
   }

   Threading::UnlockMutex(pQuery->accessMutex);
}


double CometSearch::GetFragmentIonMass(int iWhichIonSeries,
                                       int i,
                                       int ctCharge,
                                       double *_pdAAforward,
                                       double *_pdAAreverse)
{
   double dFragmentIonMass = 0.0;

   switch (iWhichIonSeries)
   {
      case ION_SERIES_B:
         dFragmentIonMass = _pdAAforward[i];
         break;
      case ION_SERIES_Y:
         dFragmentIonMass = _pdAAreverse[i];
         break;
      case ION_SERIES_A:
         dFragmentIonMass = _pdAAforward[i] - g_staticParams.massUtility.dCO;
         break;
      case ION_SERIES_C:
         dFragmentIonMass = _pdAAforward[i] + g_staticParams.massUtility.dNH3;
         break;
      case ION_SERIES_X:
         dFragmentIonMass = _pdAAreverse[i] + g_staticParams.massUtility.dCOminusH2;
         break;
      case ION_SERIES_Z:
         dFragmentIonMass = _pdAAreverse[i] - g_staticParams.massUtility.dNH2;
         break;
   }

   return (dFragmentIonMass + (ctCharge-1)*PROTON_MASS)/ctCharge;
}


void CometSearch::StorePeptide(int iWhichQuery,
                               int iStartPos,
                               int iEndPos,
                               bool bFoundVariableMod,
                               char *szProteinSeq,
                               double dCalcPepMass,
                               double dXcorr,
                               char *szProteinName,
                               bool bStoreSeparateDecoy,
                               char *pcVarModSites)
{
   int i;
   int iLenPeptide;
   Query* pQuery = g_pvQuery.at(iWhichQuery);

   iLenPeptide = iEndPos - iStartPos + 1;

   if (iLenPeptide >= MAX_PEPTIDE_LEN)
      return;

   if (bStoreSeparateDecoy)
   {
      short siLowestDecoySpScoreIndex;

      siLowestDecoySpScoreIndex = pQuery->siLowestDecoySpScoreIndex;

      pQuery->iDoDecoyXcorrCount++;
      pQuery->_pDecoys[siLowestDecoySpScoreIndex].iLenPeptide=iLenPeptide;

      memcpy(pQuery->_pDecoys[siLowestDecoySpScoreIndex].szPeptide, szProteinSeq+iStartPos, iLenPeptide);
      pQuery->_pDecoys[siLowestDecoySpScoreIndex].szPeptide[iLenPeptide]='\0';

      pQuery->_pDecoys[siLowestDecoySpScoreIndex].dPepMass = dCalcPepMass;

      if (pQuery->_spectrumInfoInternal.iChargeState > 2)
      {
         pQuery->_pDecoys[siLowestDecoySpScoreIndex].iTotalIons 
               = (iLenPeptide-1)*(pQuery->_spectrumInfoInternal.iChargeState-1) 
                   * g_staticParams.ionInformation.iNumIonSeriesUsed;
      }
      else
      {
          pQuery->_pDecoys[siLowestDecoySpScoreIndex].iTotalIons
               = (iLenPeptide-1)*g_staticParams.ionInformation.iNumIonSeriesUsed;
      }

      pQuery->_pDecoys[siLowestDecoySpScoreIndex].fXcorr = (float)dXcorr;

      pQuery->_pDecoys[siLowestDecoySpScoreIndex].iDuplicateCount = 0;

      if (iStartPos == 0)
         pQuery->_pDecoys[siLowestDecoySpScoreIndex].szPrevNextAA[0] = '-';
      else
         pQuery->_pDecoys[siLowestDecoySpScoreIndex].szPrevNextAA[0] = szProteinSeq[iStartPos - 1];

      if (iEndPos == _proteinInfo.iProteinSeqLength-1)
         pQuery->_pDecoys[siLowestDecoySpScoreIndex].szPrevNextAA[1] = '-';
      else
         pQuery->_pDecoys[siLowestDecoySpScoreIndex].szPrevNextAA[1] = szProteinSeq[iEndPos + 1];

      strcpy(pQuery->_pDecoys[siLowestDecoySpScoreIndex].szProtein, szProteinName);

      if (g_staticParams.variableModParameters.bVarModSearch)
      {
         if (!bFoundVariableMod)   // Normal peptide in variable mod search.
         {
            memset(pQuery->_pDecoys[siLowestDecoySpScoreIndex].pcVarModSites,
                  0, _iSizepcVarModSites);
         }
         else
         {
            memcpy(pQuery->_pDecoys[siLowestDecoySpScoreIndex].pcVarModSites,
                  pcVarModSites, _iSizepcVarModSites);
         }
      }

      // Get new lowest score.
      pQuery->fLowestDecoyCorrScore = pQuery->_pDecoys[0].fXcorr;
      siLowestDecoySpScoreIndex=0;

      for (i=1; i<g_staticParams.options.iNumStored; i++)
      {
         if (pQuery->_pDecoys[i].fXcorr < pQuery->fLowestDecoyCorrScore)
         {
            pQuery->fLowestDecoyCorrScore = pQuery->_pDecoys[i].fXcorr;
            siLowestDecoySpScoreIndex = i;
         }
      }

      pQuery->siLowestDecoySpScoreIndex = siLowestDecoySpScoreIndex;
   }
   else
   {
      short siLowestSpScoreIndex;

      siLowestSpScoreIndex = pQuery->siLowestSpScoreIndex;

      pQuery->iDoXcorrCount++;
      pQuery->_pResults[siLowestSpScoreIndex].iLenPeptide=iLenPeptide;

      memcpy(pQuery->_pResults[siLowestSpScoreIndex].szPeptide, szProteinSeq+iStartPos, iLenPeptide);
      pQuery->_pResults[siLowestSpScoreIndex].szPeptide[iLenPeptide]='\0';

      pQuery->_pResults[siLowestSpScoreIndex].dPepMass = dCalcPepMass;

      if (pQuery->_spectrumInfoInternal.iChargeState > 2)
      {
         pQuery->_pResults[siLowestSpScoreIndex].iTotalIons 
               = (iLenPeptide-1)*(pQuery->_spectrumInfoInternal.iChargeState-1) 
                   * g_staticParams.ionInformation.iNumIonSeriesUsed;
      }
      else
      {
          pQuery->_pResults[siLowestSpScoreIndex].iTotalIons
               = (iLenPeptide-1)*g_staticParams.ionInformation.iNumIonSeriesUsed;
      }

      if (dXcorr < 0.0)
         dXcorr = 0.0;

      pQuery->_pResults[siLowestSpScoreIndex].fXcorr = (float)dXcorr;

      pQuery->_pResults[siLowestSpScoreIndex].iDuplicateCount = 0;

      if (iStartPos == 0)
         pQuery->_pResults[siLowestSpScoreIndex].szPrevNextAA[0] = '-';
      else
         pQuery->_pResults[siLowestSpScoreIndex].szPrevNextAA[0] = szProteinSeq[iStartPos - 1];

      if (iEndPos == _proteinInfo.iProteinSeqLength-1)
         pQuery->_pResults[siLowestSpScoreIndex].szPrevNextAA[1] = '-';
      else
         pQuery->_pResults[siLowestSpScoreIndex].szPrevNextAA[1] = szProteinSeq[iEndPos + 1];

      strcpy(pQuery->_pResults[siLowestSpScoreIndex].szProtein, szProteinName);

      if (g_staticParams.variableModParameters.bVarModSearch)
      {
         if (!bFoundVariableMod)  // Normal peptide in variable mod search.
         {
            memset(pQuery->_pResults[siLowestSpScoreIndex].pcVarModSites,
                  0, _iSizepcVarModSites);
         }
         else
         {
            memcpy(pQuery->_pResults[siLowestSpScoreIndex].pcVarModSites,
                  pcVarModSites, _iSizepcVarModSites);
         }
      }

      // Get new lowest score.
      pQuery->fLowestCorrScore = pQuery->_pResults[0].fXcorr;
      siLowestSpScoreIndex=0;

      for (i=1; i<g_staticParams.options.iNumStored; i++)
      {
         if (pQuery->_pResults[i].fXcorr < pQuery->fLowestCorrScore)
         {
            pQuery->fLowestCorrScore = pQuery->_pResults[i].fXcorr;
            siLowestSpScoreIndex = i;
         }
      }

      pQuery->siLowestSpScoreIndex = siLowestSpScoreIndex;
   }
}


int CometSearch::CheckDuplicate(int iWhichQuery,
                                int iStartPos,
                                int iEndPos,
                                bool bFoundVariableMod,
                                double dCalcPepMass,
                                char *szProteinSeq,
                                char *szProteinName,
                                bool bDecoyResults,
                                char *pcVarModSites)
{
   int i,
       iLenMinus1,
       bIsDuplicate=0;
   Query* pQuery = g_pvQuery.at(iWhichQuery);

   iLenMinus1 = iEndPos-iStartPos+1;

   if (bDecoyResults)
   {
      for (i=0; i<g_staticParams.options.iNumStored; i++)
      {        
         // Quick check of peptide sequence length first.
         if ( (iLenMinus1 == pQuery->_pDecoys[i].iLenPeptide)
             && fabs(dCalcPepMass - pQuery->_pDecoys[i].dPepMass)<=FLOAT_ZERO )
         {
            if (pQuery->_pDecoys[i].szPeptide[0] == szProteinSeq[iStartPos])
            {
               if (!memcmp(pQuery->_pDecoys[i].szPeptide,
                        szProteinSeq + iStartPos, pQuery->_pDecoys[i].iLenPeptide))
               {
                  bIsDuplicate=1;
               }
            }

            // If bIsDuplicate & variable mod search, check modification sites to see if peptide already stored.
            if (bIsDuplicate && g_staticParams.variableModParameters.bVarModSearch && bFoundVariableMod)
            {
               if (!memcmp(pcVarModSites, pQuery->_pDecoys[i].pcVarModSites,
                        pQuery->_pDecoys[i].iLenPeptide + 2))
               {
                  bIsDuplicate=1;
               }
               else
               {
                  bIsDuplicate=0;
               }
            }

            if (bIsDuplicate)
            {
               pQuery->_pDecoys[i].iDuplicateCount++;
               break;
            }
         }
      }
   }
   else
   {
      for (i=0; i<g_staticParams.options.iNumStored; i++)
      {
         // Quick check of peptide sequence length.
         if ( (iLenMinus1 == pQuery->_pResults[i].iLenPeptide)
             && fabs(dCalcPepMass - pQuery->_pResults[i].dPepMass)<=FLOAT_ZERO )
         {
            if (pQuery->_pResults[i].szPeptide[0] == szProteinSeq[iStartPos])
            {
               if (!memcmp(pQuery->_pResults[i].szPeptide, szProteinSeq + iStartPos,
                        pQuery->_pResults[i].iLenPeptide))
               {
                  bIsDuplicate=1;
               }
            }

            // If bIsDuplicate & variable mod search, check modification sites to see if peptide already stored.
            if (bIsDuplicate && g_staticParams.variableModParameters.bVarModSearch && bFoundVariableMod)
            {
               if (!memcmp(pcVarModSites, pQuery->_pResults[i].pcVarModSites,
                        pQuery->_pResults[i].iLenPeptide + 2))
               {
                  bIsDuplicate=1;
               }
               else
               {
                  bIsDuplicate=0;
               }
            }

            if (bIsDuplicate)
            {
               pQuery->_pResults[i].iDuplicateCount++;
               break;
            }
         }
      }
   }

   return (bIsDuplicate);
}


void CometSearch::SubtractVarMods(int *piVarModCounts,
                                int character)
{
   if (g_staticParams.variableModParameters.bVarModSearch)
   {
      int i;
      for (i=0; i<VMODS; i++)
      {
         if ((g_staticParams.variableModParameters.varModList[i].dVarModMass != 0.0) &&
               strchr(g_staticParams.variableModParameters.varModList[i].szVarModChar, character))
         {
            piVarModCounts[i]--;
         }
      }
   }
}


void CometSearch::CountVarMods(int *piVarModCounts,
                             int character) 
{
   int i;
   for (i=0; i<VMODS; i++)
   {
      if ((g_staticParams.variableModParameters.varModList[i].dVarModMass != 0.0)
            && strchr(g_staticParams.variableModParameters.varModList[i].szVarModChar, character))
      {
         piVarModCounts[i]++;
      }
   }
}


void CometSearch::CountBinaryModN(int *piVarModCounts,
                                   int iStartPos)
{
   if ((g_staticParams.variableModParameters.dVarModMassN != 0.0)
         && ((g_staticParams.variableModParameters.iVarModNtermDistance == -1)
              || (iStartPos <= g_staticParams.variableModParameters.iVarModNtermDistance)))
   {
      piVarModCounts[VMOD_N_INDEX] = 1;
   }
   else
   {
      piVarModCounts[VMOD_N_INDEX] = 0;
   }
}


void CometSearch::CountBinaryModC(int *piVarModCounts,
                                   int iEndPos)
{
   if ((g_staticParams.variableModParameters.dVarModMassC != 0.0)
         && ((g_staticParams.variableModParameters.iVarModCtermDistance == -1) 
              || ((iEndPos + g_staticParams.variableModParameters.iVarModCtermDistance) 
                     >= (_proteinInfo.iProteinSeqLength-1))))
   {
      piVarModCounts[VMOD_C_INDEX] = 1;
   }
   else
   {
      piVarModCounts[VMOD_C_INDEX] = 0;
   }
}


int CometSearch::TotalVarModCount(int *pVarModCounts,
                                int iCVarModCount,
                                int iNVarModCount)
{
   int i;
   int iTotalVarMods= 0;

   for (i=0; i<VMODS; i++)
   {
      iTotalVarMods += pVarModCounts[i];
   }

   iTotalVarMods += iNVarModCount;
   iTotalVarMods += iCVarModCount;

   return iTotalVarMods;
}


void CometSearch::VarModSearch(char *szProteinSeq,
                             char *szProteinName,
                             int varModCounts[],
                             int iStartPos,
                             int iEndPos)
{
   int i,
       i1,
       i2,
       i3,
       i4,
       i5,
       i6,
       iN,
       iC,
       numVarModCounts[VMODS_ALL];
   double dTmpNum;

   strcpy(_proteinInfo.szProteinName, szProteinName);

   for (i=0; i<VMODS; i++)
   {
      numVarModCounts[i] = varModCounts[i] > g_staticParams.variableModParameters.varModList[i].iMaxNumVarModAAPerMod
         ? g_staticParams.variableModParameters.varModList[i].iMaxNumVarModAAPerMod : varModCounts[i];
   }

   dTmpNum = g_staticParams.precalcMasses.dOH2ProtonCtermNterm;

   if (iStartPos == 0)
      dTmpNum += g_staticParams.staticModifications.dAddNterminusProtein;
   if (iEndPos == _proteinInfo.iProteinSeqLength-1)
      dTmpNum += g_staticParams.staticModifications.dAddCterminusProtein;

   for (iN=0; iN<=varModCounts[VMOD_N_INDEX]; iN++)
   {
      for (i6=0; i6<=numVarModCounts[VMOD_6_INDEX]; i6++)
      {
         if (i6 > g_staticParams.variableModParameters.iMaxVarModPerPeptide)
            break;

         for (i5=0; i5<=numVarModCounts[VMOD_5_INDEX]; i5++)
         {
            int iSum5 = i6+i5;

            if (iSum5 > g_staticParams.variableModParameters.iMaxVarModPerPeptide)
               break;

            for (i4=0; i4<=numVarModCounts[VMOD_4_INDEX]; i4++)
            {
               int iSum4 = iSum5 + i4;

               if (iSum4 > g_staticParams.variableModParameters.iMaxVarModPerPeptide)
                  break;

               for (i3=0; i3<=numVarModCounts[VMOD_3_INDEX]; i3++)
               {
                  int iSum3 = iSum4 + i3;

                  if (iSum3 > g_staticParams.variableModParameters.iMaxVarModPerPeptide)
                     break;

                  for (i2=0; i2<=numVarModCounts[VMOD_2_INDEX]; i2++)
                  {
                     int iSum2 = iSum3 + i2;

                     if (iSum2 > g_staticParams.variableModParameters.iMaxVarModPerPeptide)
                        break;

                     for (i1=0; i1<=numVarModCounts[VMOD_1_INDEX]; i1++)
                     {
                        int iSum1 = iSum2 + i1;

                        if (iSum1 > g_staticParams.variableModParameters.iMaxVarModPerPeptide)
                           break;

                        int varModCounts[] = {i1, i2, i3, i4, i5, i6};

                        if ((TotalVarModCount(varModCounts, 0, iN) > 0)
                              || g_staticParams.variableModParameters.dVarModMassC != 0.0)
                        {
                           double dCalcPepMass;
                           int iTmpEnd;
                           int iStartTmp = iStartPos+1;

                           dCalcPepMass = dTmpNum + TotalVarModMass(varModCounts, 0, iN);
                           for (i=0; i<VMODS; i++)
                           {
                              _varModInfo.varModStatList[i].iTotVarModCt = 0;
                           }

                           for (iTmpEnd=iStartPos; iTmpEnd<=iEndPos; iTmpEnd++)
                           {
                              if (iTmpEnd-iStartTmp < MAX_PEPTIDE_LEN)
                              {
                                 dCalcPepMass += g_staticParams.massUtility.pdAAMassParent[(int)szProteinSeq[iTmpEnd]];
   
                                 for (i=0; i<VMODS; i++)
                                 {
                                    if ((g_staticParams.variableModParameters.varModList[i].dVarModMass != 0.0)
                                          && strchr(g_staticParams.variableModParameters.varModList[i].szVarModChar, szProteinSeq[iTmpEnd]))
                                    {
                                       _varModInfo.varModStatList[i].iTotVarModCt++;
                                    }
                                 }
   
                                 if ((g_staticParams.variableModParameters.dVarModMassC != 0.0)
                                       && ((g_staticParams.variableModParameters.iVarModCtermDistance == -1)
                                          || ((iTmpEnd + g_staticParams.variableModParameters.iVarModCtermDistance) >= (_proteinInfo.iProteinSeqLength - 1))))
                                 {
                                    numVarModCounts[VMOD_C_INDEX] = 1;
                                 }
                                 else
                                 {
                                    numVarModCounts[VMOD_C_INDEX] = 0;
                                 }
   
                                 for (iC=0; iC<=numVarModCounts[VMOD_C_INDEX]; iC++)
                                 {
                                    double dTmpCalcMass =  dCalcPepMass + iC*g_staticParams.variableModParameters.dVarModMassC;
                                    bool bValid = true;
   
                                    // Check to make sure # required mod are actually present in
                                    // current peptide since the end position is variable.
                                    for (i=0; i<VMODS; i++)
                                    {
                                       // varModStatList[i].iTotVarModC contains # of mod residues in current
                                       // peptide defined by iTmpEnd.  Since varModCounts contains # of
                                       // each variable mod to match peptide mass, need to make sure that
                                       // varModCounts is not greater than varModStatList[i].iTotVarModC.
                                       // Moreso, if a binary mod search is being performed, these
                                       // values have to be the same.

                                       if (g_staticParams.variableModParameters.varModList[i].bBinaryMod)
                                       {
                                          if (varModCounts[i] != 0 && varModCounts[i] != _varModInfo.varModStatList[i].iTotVarModCt)
                                          {
                                             bValid = false;
                                             break;
                                          }
                                       }
                                       else
                                       {
                                          if (varModCounts[i] > _varModInfo.varModStatList[i].iTotVarModCt)
                                          {
                                             bValid = false;
                                             break;
                                          }
                                       }
                                    }

                                    if (bValid && TotalVarModCount(varModCounts, iC, iN) > 0)
                                    {
                                       int iWhichQuery = WithinMassTolerance(dTmpCalcMass, szProteinSeq, iStartPos, iTmpEnd);
   
                                       if (iWhichQuery != -1)
                                       {
                                          // We know that mass is within some query's tolerance range so
                                          // now need to permute variable mods and at each permutation calculate
                                          // fragment ions once and loop through all matching spectra to score.
                                          for (i=0; i<VMODS; i++)
                                          {
                                             if (g_staticParams.variableModParameters.varModList[i].dVarModMass > 0.0  && varModCounts[i] > 0)
                                             {
                                                memset(_varModInfo.varModStatList[i].iVarModSites, 0, sizeof(_varModInfo.varModStatList[i].iVarModSites));
                                             }
   
                                             _varModInfo.varModStatList[i].iMatchVarModCt = varModCounts[i];
                                          }
   
                                          _varModInfo.iNumVarModSiteN = iN;
                                          _varModInfo.iNumVarModSiteC = iC;
   
                                          _varModInfo.iStartPos = iStartPos;
                                          _varModInfo.iEndPos = iTmpEnd;
   
                                          _varModInfo.dCalcPepMass = dTmpCalcMass;
   
                                          Permute1(szProteinSeq, iWhichQuery);
                                       }
                                    }
                                 }
                              }
                           }
                        }
                     }
                  }
               }
            }
         }
      }
   }
}


double CometSearch::TotalVarModMass(int *pVarModCounts,
                                  int iCVarModCount,
                                  int iNVarModCount)
{
   double dTotVarModMass = 0;

   int i;
   for (i=0; i<VMODS; i++)
      dTotVarModMass += g_staticParams.variableModParameters.varModList[i].dVarModMass * pVarModCounts[i];

   dTotVarModMass += g_staticParams.variableModParameters.dVarModMassN * iNVarModCount;
   dTotVarModMass += g_staticParams.variableModParameters.dVarModMassC * iCVarModCount;

   return dTotVarModMass;
}


void CometSearch::Permute1(char *szProteinSeq,
                           int iWhichQuery)
{
   if (_varModInfo.varModStatList[VMOD_1_INDEX].iMatchVarModCt > 0)
   {
      int p1[MAX_PEPTIDE_LEN_P2];
      int b1[MAX_PEPTIDE_LEN];

      int i, x, y, z;

      int N1 = _varModInfo.varModStatList[VMOD_1_INDEX].iTotVarModCt;
      int M1 = _varModInfo.varModStatList[VMOD_1_INDEX].iMatchVarModCt;

      inittwiddle(M1, N1, p1);

      for (i=0; i != N1-M1; i++)
      {
         _varModInfo.varModStatList[VMOD_1_INDEX].iVarModSites[i] = 0;
         b1[i] = 0;
      }

      while (i != N1)
      {
         _varModInfo.varModStatList[VMOD_1_INDEX].iVarModSites[i] = 1;
         b1[i] = 1;
         i++;
      }

      Permute2(szProteinSeq, iWhichQuery);

      while (!twiddle(&x, &y, &z, p1))
      {
         b1[x] = 1;
         b1[y] = 0;

         for (i=0; i != N1; i++)
            _varModInfo.varModStatList[VMOD_1_INDEX].iVarModSites[i] = (b1[i] ? 1 : 0);

         Permute2(szProteinSeq, iWhichQuery);
      }
   }
   else
      Permute2(szProteinSeq, iWhichQuery);
}


void CometSearch::Permute2(char *szProteinSeq,
                           int iWhichQuery)
{
   if (_varModInfo.varModStatList[VMOD_2_INDEX].iMatchVarModCt > 0)
   {
      int p2[MAX_PEPTIDE_LEN_P2];
      int b2[MAX_PEPTIDE_LEN];

      int i, x, y, z;

      int N2 = _varModInfo.varModStatList[VMOD_2_INDEX].iTotVarModCt;
      int M2 = _varModInfo.varModStatList[VMOD_2_INDEX].iMatchVarModCt;

      inittwiddle(M2, N2, p2);

      for (i=0; i != N2-M2; i++)
      {
         _varModInfo.varModStatList[VMOD_2_INDEX].iVarModSites[i] = 0;
         b2[i] = 0;
      }

      while (i != N2)
      {
         _varModInfo.varModStatList[VMOD_2_INDEX].iVarModSites[i] = 2;
         b2[i] = 1;
         i++;
      }

      Permute3(szProteinSeq, iWhichQuery);

      while (!twiddle(&x, &y, &z, p2))
      {
         b2[x] = 1;
         b2[y] = 0;

         for (i=0; i != N2; i++)
            _varModInfo.varModStatList[VMOD_2_INDEX].iVarModSites[i] = (b2[i] ? 2 : 0);

         Permute3(szProteinSeq, iWhichQuery);
      }
   }
   else
   {
      Permute3(szProteinSeq, iWhichQuery);
   }
}


void CometSearch::Permute3(char *szProteinSeq,
                           int iWhichQuery)
{
   if (_varModInfo.varModStatList[VMOD_3_INDEX].iMatchVarModCt > 0)
   {
      int p3[MAX_PEPTIDE_LEN_P2];
      int b3[MAX_PEPTIDE_LEN];

      int i, x, y, z;

      int N3 = _varModInfo.varModStatList[VMOD_3_INDEX].iTotVarModCt;
      int M3 = _varModInfo.varModStatList[VMOD_3_INDEX].iMatchVarModCt;

      inittwiddle(M3, N3, p3);

      for (i=0; i != N3-M3; i++)
      {
         _varModInfo.varModStatList[VMOD_3_INDEX].iVarModSites[i] = 0;
         b3[i] = 0;
      }

      while (i != N3)
      {
         _varModInfo.varModStatList[VMOD_3_INDEX].iVarModSites[i] = 3;
         b3[i] = 1;
         i++;
      }

      Permute4(szProteinSeq, iWhichQuery);

      while (!twiddle(&x, &y, &z, p3))
      {
         b3[x] = 1;
         b3[y] = 0;

         for (i=0; i != N3; i++)
            _varModInfo.varModStatList[VMOD_3_INDEX].iVarModSites[i] = (b3[i] ? 3 : 0);

         Permute4(szProteinSeq, iWhichQuery);
      }
   }
   else
   {
      Permute4(szProteinSeq, iWhichQuery);
   }
}


void CometSearch::Permute4(char *szProteinSeq,
                           int iWhichQuery)
{
   if (_varModInfo.varModStatList[VMOD_4_INDEX].iMatchVarModCt > 0)
   {
      int p4[MAX_PEPTIDE_LEN_P2];
      int b4[MAX_PEPTIDE_LEN];

      int i, x, y, z;

      int N4 = _varModInfo.varModStatList[VMOD_4_INDEX].iTotVarModCt;
      int M4 = _varModInfo.varModStatList[VMOD_4_INDEX].iMatchVarModCt;

      inittwiddle(M4, N4, p4);

      for (i=0; i != N4-M4; i++)
      {
         _varModInfo.varModStatList[VMOD_4_INDEX].iVarModSites[i] = 0;
         b4[i] = 0;
      }

      while (i != N4)
      {
         _varModInfo.varModStatList[VMOD_4_INDEX].iVarModSites[i] = 4;
         b4[i] = 1;
         i++;
      }

      Permute5(szProteinSeq, iWhichQuery);

      while (!twiddle(&x, &y, &z, p4))
      {
         b4[x] = 1;
         b4[y] = 0;

         for (i=0; i != N4; i++)
            _varModInfo.varModStatList[VMOD_4_INDEX].iVarModSites[i] = (b4[i] ? 4 : 0);

         Permute5(szProteinSeq, iWhichQuery);
      }
   }
   else
   {
       Permute5(szProteinSeq, iWhichQuery);
   }
}


void CometSearch::Permute5(char *szProteinSeq,
                           int iWhichQuery)
{
   if (_varModInfo.varModStatList[VMOD_5_INDEX].iMatchVarModCt > 0)
   {
      int p5[MAX_PEPTIDE_LEN_P2];
      int b5[MAX_PEPTIDE_LEN];

      int i, x, y, z;

      int N5 = _varModInfo.varModStatList[VMOD_5_INDEX].iTotVarModCt;
      int M5 = _varModInfo.varModStatList[VMOD_5_INDEX].iMatchVarModCt;

      inittwiddle(M5, N5, p5);

      for (i=0; i != N5-M5; i++)
      {
         _varModInfo.varModStatList[VMOD_5_INDEX].iVarModSites[i] = 0;
         b5[i] = 0;
      }

      while (i != N5)
      {
         _varModInfo.varModStatList[VMOD_5_INDEX].iVarModSites[i] = 5;
         b5[i] = 1;
         i++;
      }

      Permute6(szProteinSeq, iWhichQuery);

      while (!twiddle(&x, &y, &z, p5))
      {
         b5[x] = 1;
         b5[y] = 0;

         for (i=0; i != N5; i++)
            _varModInfo.varModStatList[VMOD_5_INDEX].iVarModSites[i] = (b5[i] ? 5 : 0);

         Permute6(szProteinSeq, iWhichQuery);
      }
   }
   else
   {
      Permute6(szProteinSeq, iWhichQuery);
   }
}


void CometSearch::Permute6(char *szProteinSeq,
                           int iWhichQuery)
{
   if (_varModInfo.varModStatList[VMOD_6_INDEX].iMatchVarModCt > 0)
   {
      int p6[MAX_PEPTIDE_LEN_P2];
      int b6[MAX_PEPTIDE_LEN];

      int i, x, y, z;

      int N6 = _varModInfo.varModStatList[VMOD_6_INDEX].iTotVarModCt;
      int M6 = _varModInfo.varModStatList[VMOD_6_INDEX].iMatchVarModCt;

      inittwiddle(M6, N6, p6);

      for (i=0; i != N6-M6; i++)
      {
         _varModInfo.varModStatList[VMOD_6_INDEX].iVarModSites[i] = 0;
         b6[i] = 0;
      }

      while (i != N6)
      {
         _varModInfo.varModStatList[VMOD_6_INDEX].iVarModSites[i] = 6;
         b6[i] = 1;
         i++;
      }

      CalcVarModIons(szProteinSeq, iWhichQuery);

      while (!twiddle(&x, &y, &z, p6))
      {
         b6[x] = 1;
         b6[y] = 0;

         for (i=0; i != N6; i++)
            _varModInfo.varModStatList[VMOD_6_INDEX].iVarModSites[i] = (b6[i] ? 6 : 0);

         CalcVarModIons(szProteinSeq, iWhichQuery);
      }
   }
   else
   {
      CalcVarModIons(szProteinSeq, iWhichQuery);
   }
}


/*
  twiddle.c - generate all combinations of M elements drawn without replacement
  from a set of N elements.  This routine may be used in two ways:
  (0) To generate all combinations of M out of N objects, let a[0..N-1]
      contain the objects, and let c[0..M-1] initially be the combination
      a[N-M..N-1].  While twiddle(&x, &y, &z, p) is false, set c[z] = a[x] to
      produce a new combination.
  (1) To generate all sequences of 0's and 1's containing M 1's, let
      b[0..N-M-1] = 0 and b[N-M..N-1] = 1.  While twiddle(&x, &y, &z, p) is
      false, set b[x] = 1 and b[y] = 0 to produce a new sequence.

  In either of these cases, the array p[0..N+1] should be initialised as
  follows:
    p[0] = N+1
    p[1..N-M] = 0
    p[N-M+1..N] = 1..M
    p[N+1] = -2
    if M=0 then p[1] = 1

  In this implementation, this initialisation is accomplished by calling
  inittwiddle(M, N, p), where p points to an array of N+2 ints.

  Coded by Matthew Belmonte <mkb4@Cornell.edu>, 23 March 1996.  This
  implementation Copyright (c) 1996 by Matthew Belmonte.  Permission for use and
  distribution is hereby granted, subject to the restrictions that this
  copyright notice and reference list be included in its entirety, and that any
  and all changes made to the program be clearly noted in the program text.

  This software is provided 'as is', with no warranty, express or implied,
  including but not limited to warranties of merchantability or fitness for a
  particular purpose.  The user of this software assumes liability for any and
  all damages, whether direct or consequential, arising from its use.  The
  author of this implementation will not be liable for any such damages.

  Reference:

  Phillip J Chase, `Algorithm 382: Combinations of M out of N Objects [G6]',
  Communications of the Association for Computing Machinery 13:6:368 (1970).

  The returned indices x, y, and z in this implementation are decremented by 1,
  in order to conform to the C language array reference convention.  Also, the
  parameter 'done' has been replaced with a Boolean return value.
*/

int CometSearch::twiddle(int *x, int *y, int *z, int *p)
{
   register int i, j, k;
   j = 1;

   while (p[j] <= 0)
      j++;

   if (p[j - 1] == 0)
   {
      for (i=j-1; i != 1; i--)
         p[i] = -1;
      p[j] = 0;
      *x = *z = 0;
      p[1] = 1;
      *y = j - 1;
   }
   else
   {
      if (j > 1)
         p[j - 1] = 0;
      do
         j++;

      while (p[j] > 0);

      k = j - 1;
      i = j;

      while (p[i] == 0)
         p[i++] = -1;

      if (p[i] == -1)
      {
         p[i] = p[k];
         *z = p[k] - 1;
         *x = i - 1;
         *y = k - 1;
         p[k] = -1;
      }
      else
      {
         if (i == p[0])
            return (1);
         else
         {
            p[j] = p[i];
            *z = p[i] - 1;
            p[i] = 0;
            *x = j - 1;
            *y = i - 1;
         }
      }
   }
   return (0);
}


void CometSearch::inittwiddle(int m, int n, int *p)
{
   int i;

   p[0] = n + 1;

   for (i=1; i != n-m+1; i++)
      p[i] = 0;

   while (i != n+1)
   {
      p[i] = i + m - n;
      i++;
   }

   p[n + 1] = -2;

   if (m == 0)
      p[1] = 1;
}


void CometSearch::CalcVarModIons(char *szProteinSeq,
                              int iWhichQuery)
{
   int iLenPeptide = 0;
   char pcVarModSites[MAX_PEPTIDE_LEN_P2];
   char pcVarModSitesDecoy[MAX_PEPTIDE_LEN_P2];
   char szDecoyPeptide[MAX_PEPTIDE_LEN_P2];  // allow for prev/next AA in string
   char szDecoyProteinName[WIDTH_REFERENCE];
   int ctIonSeries;
   int ctLen;
   int ctCharge;
   int iWhichIonSeries;

   // at this point, need to compare current modified peptide
   // against all relevant entries

   bool bFirstTimeThroughLoopForPeptide = true;

   // Compare calculated fragment ions against all matching query spectra

   while (iWhichQuery < (int)g_pvQuery.size())
   {
      if (_varModInfo.dCalcPepMass < g_pvQuery.at(iWhichQuery)->_pepMassInfo.dPeptideMassToleranceMinus)
      {
         // if calculated mass is smaller than low mass range, it
         // means we reached candidate peptides that are too big
         break;
      }

      // Mass tolerance check for particular query against this candidate peptide mass
      if (CheckMassMatch(iWhichQuery, _varModInfo.dCalcPepMass))
      {
         int iLenMinus1 = _varModInfo.iEndPos - _varModInfo.iStartPos;     // equals iLenPeptide-1

         // Calculate ion series just once to compare against all relevant query spectra
         if (bFirstTimeThroughLoopForPeptide)
         {
            bool *pbDuplFragment;

            bFirstTimeThroughLoopForPeptide = false;

            int i;
            int j;
            int piVarModCharIdx[VMODS];
            int iTmp =_varModInfo.iEndPos - _varModInfo.iStartPos + 1;
            double dBion = g_staticParams.precalcMasses.dNtermProton;
            double dYion = g_staticParams.precalcMasses.dCtermOH2Proton;


            if (_varModInfo.iStartPos == 0)
               dBion += g_staticParams.staticModifications.dAddNterminusProtein;
            if (_varModInfo.iEndPos == _proteinInfo.iProteinSeqLength-1)
               dYion += g_staticParams.staticModifications.dAddCterminusProtein;

            // contains positional coding of a variable mod at each index which equals an AA residue
            memset(pcVarModSites, 0, _iSizepcVarModSites);
            memset(piVarModCharIdx, 0, sizeof(piVarModCharIdx));

            if (_varModInfo.iNumVarModSiteN == 1)
            {
               dBion += g_staticParams.variableModParameters.dVarModMassN;
               pcVarModSites[iTmp] = 1;
            }

            if (_varModInfo.iNumVarModSiteC == 1)
            {
               dYion += g_staticParams.variableModParameters.dVarModMassC;
               pcVarModSites[iTmp + 1] = 1;
            }

            // Generate pdAAforward for _pResults[0].szPeptide
            for (i=_varModInfo.iStartPos; i<=_varModInfo.iEndPos; i++)
            {
               int iPos = i - _varModInfo.iStartPos;

               dBion += g_staticParams.massUtility.pdAAMassFragment[(int)szProteinSeq[i]];

               // This loop is where all individual variable mods are combined
               for (j=0; j<VMODS; j++)
               {
                  if ((g_staticParams.variableModParameters.varModList[j].dVarModMass != 0.0)
                        && (_varModInfo.varModStatList[j].iMatchVarModCt > 0)
                        && strchr(g_staticParams.variableModParameters.varModList[j].szVarModChar, szProteinSeq[i]))
                  {
                     if (_varModInfo.varModStatList[j].iVarModSites[piVarModCharIdx[j]])
                     {
                        if (pcVarModSites[iPos] != 0)  // conflict in two variable mods on same residue
                           return;
         
                        // store the modification number at modification position
                        pcVarModSites[iPos] = _varModInfo.varModStatList[j].iVarModSites[piVarModCharIdx[j]];
                        dBion += g_staticParams.variableModParameters.varModList[j].dVarModMass;
                     }
                     piVarModCharIdx[j]++;
                  }
               }

               _pdAAforward[iPos] = dBion;
            }

            for (i=_varModInfo.iEndPos; i>=_varModInfo.iStartPos; i--)
            {
               int iPos = i - _varModInfo.iStartPos ;
         
               dYion += g_staticParams.massUtility.pdAAMassFragment[(int)szProteinSeq[i]];
         
               if (pcVarModSites[iPos] > 0)
                  dYion += g_staticParams.variableModParameters.varModList[pcVarModSites[iPos]-1].dVarModMass;

               _pdAAreverse[_varModInfo.iEndPos - i] = dYion;
            }

            // now get the set of binned fragment ions once for all matching peptides
            if ((pbDuplFragment = (bool*)malloc(g_pvQuery.at(iWhichQuery)->_spectrumInfoInternal.iArraySize * (size_t)sizeof(bool)))==NULL)
            {
               fprintf(stderr, " Error - malloc pbDuplFragments; iWhichQuery = %d\n\n", iWhichQuery);
               exit(1);
            }

            for (ctIonSeries=0; ctIonSeries<g_staticParams.ionInformation.iNumIonSeriesUsed; ctIonSeries++)
            {
               iWhichIonSeries = g_staticParams.ionInformation.piSelectedIonSeries[ctIonSeries];
               for (ctCharge=1; ctCharge<=g_massRange.iMaxFragmentCharge; ctCharge++)
               {
                  for (ctLen=0; ctLen<iLenMinus1; ctLen++)
                     pbDuplFragment[BIN(GetFragmentIonMass(iWhichIonSeries, ctLen, ctCharge, _pdAAforward, _pdAAreverse))] = false;
               }
            }

            for (ctIonSeries=0; ctIonSeries<g_staticParams.ionInformation.iNumIonSeriesUsed; ctIonSeries++)
            {
               iWhichIonSeries = g_staticParams.ionInformation.piSelectedIonSeries[ctIonSeries];

               for (ctCharge=1; ctCharge<=g_massRange.iMaxFragmentCharge; ctCharge++)
               {
                  // as both _pdAAforward and _pdAAreverse are increasing, loop through
                  // iLenPeptide-1 to complete set of internal fragment ions
                  for (ctLen=0; ctLen<iLenMinus1; ctLen++)
                  {
                     int iVal = BIN(GetFragmentIonMass(iWhichIonSeries, ctLen, ctCharge, _pdAAforward, _pdAAreverse));

                     if (pbDuplFragment[iVal] == false)
                     {
                        _uiBinnedIonMasses[ctCharge][ctIonSeries][ctLen] = iVal;
                        pbDuplFragment[iVal] = true;
                     }
                     else
                        _uiBinnedIonMasses[ctCharge][ctIonSeries][ctLen] = 0;
                  }
               }
            }

            iLenPeptide = iLenMinus1+1;

            // Also take care of decoy here
            if (g_staticParams.options.iDecoySearch)
            {
               char pcTmpVarModSearchSites[MAX_PEPTIDE_LEN_P2];  // placeholder to reverse variable mods

#ifdef _WIN32
               _snprintf(szDecoyProteinName, WIDTH_REFERENCE, "DECOY_%s", _proteinInfo.szProteinName);
               szDecoyProteinName[WIDTH_REFERENCE-1]=0;    // _snprintf does not guarantee null termination
#else
               snprintf(szDecoyProteinName, WIDTH_REFERENCE, "DECOY_%s", _proteinInfo.szProteinName);
#endif

               // Generate reverse peptide.  Keep prev and next AA in szDecoyPeptide string.
               // So actual reverse peptide starts at position 1 and ends at len-2 (as len-1
               // is next AA).

               // Store flanking residues from original sequence.
               if (_varModInfo.iStartPos==0)
                  szDecoyPeptide[0]='-';
               else
                  szDecoyPeptide[0]=szProteinSeq[_varModInfo.iStartPos-1];

               if (_varModInfo.iEndPos == _proteinInfo.iProteinSeqLength-1)
                  szDecoyPeptide[iLenPeptide+1]='-';
               else
                  szDecoyPeptide[iLenPeptide+1]=szProteinSeq[_varModInfo.iEndPos+1];

               szDecoyPeptide[iLenPeptide+2]='\0';

               // Now reverse the peptide and reverse the variable mod locations too
               if (g_staticParams.enzymeInformation.iSearchEnzymeOffSet==1)
               {
                  // last residue stays the same:  change ABCDEK to EDCBAK

                  for (i=_varModInfo.iEndPos-1; i>=_varModInfo.iStartPos; i--)
                  {
                     szDecoyPeptide[_varModInfo.iEndPos-i] = szProteinSeq[i];
                     pcTmpVarModSearchSites[_varModInfo.iEndPos-i-1] = pcVarModSites[i- _varModInfo.iStartPos];
                  }

                  szDecoyPeptide[_varModInfo.iEndPos - _varModInfo.iStartPos+1]=szProteinSeq[_varModInfo.iEndPos];  // last residue stays same
                  pcTmpVarModSearchSites[iLenPeptide-1] = pcVarModSites[iLenPeptide-1];
               }
               else
               {
                  // first residue stays the same:  change ABCDEK to AKEDCB

                  for (i=_varModInfo.iEndPos; i>=_varModInfo.iStartPos+1; i--)
                  {
                     szDecoyPeptide[_varModInfo.iEndPos-i+2] = szProteinSeq[i];
                     pcTmpVarModSearchSites[_varModInfo.iEndPos-i+1] = pcVarModSites[i- _varModInfo.iStartPos];
                  }

                  szDecoyPeptide[1]=szProteinSeq[_varModInfo.iStartPos];  // first residue stays same
                  pcTmpVarModSearchSites[0] = pcVarModSites[0];
               }

               pcTmpVarModSearchSites[iLenPeptide]   = pcVarModSites[iLenPeptide];    // N-term
               pcTmpVarModSearchSites[iLenPeptide+1] = pcVarModSites[iLenPeptide+1];  // C-term
               memcpy(pcVarModSitesDecoy, pcTmpVarModSearchSites, sizeof(char)*iLenPeptide+2);

               // Now need to recalculate _pdAAforward and _pdAAreverse for decoy entry
               double dBion = g_staticParams.precalcMasses.dNtermProton;
               double dYion = g_staticParams.precalcMasses.dCtermOH2Proton;

               if (_varModInfo.iStartPos == 0)
                  dBion += g_staticParams.staticModifications.dAddNterminusProtein;
               if (_varModInfo.iEndPos == _proteinInfo.iProteinSeqLength-1)
                  dYion += g_staticParams.staticModifications.dAddCterminusProtein;

               int iStartPos;  // This is start/end for newly created decoy peptide
               int iEndPos;

               iStartPos = 1;
               iEndPos = strlen(szDecoyPeptide)-2;

               int iTmp;

               if (pcVarModSitesDecoy[iLenPeptide])      // N-term mod
                  dBion += g_staticParams.variableModParameters.dVarModMassN;

               if (pcVarModSitesDecoy[iLenPeptide+1])    // C-term mod
                  dYion += g_staticParams.variableModParameters.dVarModMassC;

               // Generate pdAAforward for szDecoyPeptide
               for (i=iStartPos; i<iEndPos; i++)
               {
                  iTmp = iEndPos - (i-iStartPos);
 
                  dBion += g_staticParams.massUtility.pdAAMassFragment[(int)szDecoyPeptide[i]];
                  if (pcVarModSitesDecoy[i-iStartPos] > 0)
                     dBion += g_staticParams.variableModParameters.varModList[pcVarModSitesDecoy[i-iStartPos]-1].dVarModMass;

                  dYion += g_staticParams.massUtility.pdAAMassFragment[(int)szDecoyPeptide[iTmp]];
                  if (pcVarModSitesDecoy[iTmp-iStartPos] > 0)
                     dYion += g_staticParams.variableModParameters.varModList[pcVarModSitesDecoy[iTmp-iStartPos]-1].dVarModMass;

                  _pdAAforwardDecoy[i-iStartPos] = dBion;
                  _pdAAreverseDecoy[i-iStartPos] = dYion;
               }

               // now get the set of binned fragment ions once for all matching decoy peptides
               for (ctIonSeries=0; ctIonSeries<g_staticParams.ionInformation.iNumIonSeriesUsed; ctIonSeries++)
               {
                  iWhichIonSeries = g_staticParams.ionInformation.piSelectedIonSeries[ctIonSeries];
                  for (ctCharge = 1; ctCharge<=g_massRange.iMaxFragmentCharge; ctCharge++)
                  {
                     for (ctLen=0; ctLen<iLenMinus1; ctLen++)
                     {
                        pbDuplFragment[BIN(GetFragmentIonMass(iWhichIonSeries, ctLen, ctCharge, _pdAAforwardDecoy, _pdAAreverseDecoy))] = false;
                     }
                  }
               }

               for (ctIonSeries=0; ctIonSeries<g_staticParams.ionInformation.iNumIonSeriesUsed; ctIonSeries++)
               {
                  iWhichIonSeries = g_staticParams.ionInformation.piSelectedIonSeries[ctIonSeries];

                  for (ctCharge=1; ctCharge<=g_massRange.iMaxFragmentCharge; ctCharge++)
                  {
                     // as both _pdAAforward and _pdAAreverse are increasing, loop through
                     // iLenPeptide-1 to complete set of internal fragment ions
                     for (ctLen=0; ctLen<iLenMinus1; ctLen++)
                     {
                        int iVal = BIN(GetFragmentIonMass(iWhichIonSeries, ctLen, ctCharge, _pdAAforwardDecoy, _pdAAreverseDecoy));

                        if (pbDuplFragment[iVal] == false)
                        {
                           _uiBinnedIonMassesDecoy[ctCharge][ctIonSeries][ctLen] = iVal;
                           pbDuplFragment[iVal] = true;
                        }
                        else
                           _uiBinnedIonMassesDecoy[ctCharge][ctIonSeries][ctLen] = 0;
                     }
                  }
               }
            }

            free(pbDuplFragment);
         }

         XcorrScore(szProteinSeq, _proteinInfo.szProteinName, _varModInfo.iStartPos, _varModInfo.iEndPos, true,
               _varModInfo.dCalcPepMass, false, iWhichQuery, iLenPeptide, pcVarModSites);

         if (g_staticParams.options.iDecoySearch)
         {
            XcorrScore(szDecoyPeptide, szDecoyProteinName, 1, iLenPeptide, true,
                  _varModInfo.dCalcPepMass, true, iWhichQuery, iLenPeptide, pcVarModSitesDecoy);
         }

      }
      iWhichQuery++;
   }
}

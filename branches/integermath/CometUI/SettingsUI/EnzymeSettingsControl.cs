﻿using System;
using System.Collections.Generic;
using System.Globalization;
using System.Windows.Forms;
using CometUI.Properties;
using System.Collections.Specialized;

namespace CometUI.SettingsUI
{
    public partial class EnzymeSettingsControl : UserControl
    {
        public StringCollection EnzymeInfo { get; set; }

        private int SearchEnzymeComboEditListIndex { get; set; }
        private int SampleEnzymeComboEditListIndex { get; set; }
        private int SearchEnzymeCurrentSelectedIndex { get; set; }
        private int SampleEnzymeCurrentSelectedIndex { get; set; }

        private new SearchSettingsDlg Parent { get; set; }
        private EnzymeInfoDlg EnzymeInfoDlg { get; set; }
        private readonly Dictionary<int, string> _enzymeTermini = new Dictionary<int, string>();

        public EnzymeSettingsControl(SearchSettingsDlg parent)
        {
            InitializeComponent();

            Parent = parent;

            _enzymeTermini.Add(2, "Fully-digested");
            _enzymeTermini.Add(1, "Semi-digested");
            _enzymeTermini.Add(8, "N-term");
            _enzymeTermini.Add(9, "C-term");

            Dictionary<int, string>.KeyCollection enzymeTerminiKeys = _enzymeTermini.Keys;
            foreach (var key in enzymeTerminiKeys)
            {
                enzymeTerminiCombo.Items.Add(_enzymeTermini[key]);
            }

            InitializeFromDefaultSettings();

            EnzymeInfoDlg = new EnzymeInfoDlg(this);
        }

        public bool VerifyAndUpdateSettings()
        {
            if (Settings.Default.SearchEnzymeNumber != SearchEnzymeCurrentSelectedIndex)
            {
                Settings.Default.SearchEnzymeNumber = SearchEnzymeCurrentSelectedIndex;
                Parent.SettingsChanged = true;
            }

            if (Settings.Default.SampleEnzymeNumber != SampleEnzymeCurrentSelectedIndex)
            {
                Settings.Default.SampleEnzymeNumber = SampleEnzymeCurrentSelectedIndex;
                Parent.SettingsChanged = true;
            }

            var allowedMissedCleavages = missedCleavagesCombo.SelectedIndex;
            if (Settings.Default.AllowedMissedCleavages != allowedMissedCleavages)
            {
                Settings.Default.AllowedMissedCleavages = allowedMissedCleavages;
                Parent.SettingsChanged = true;
            }

            // Check each key in the dictionary to see which matches the value
            // currently selected in the enzyme termini combo box.
            foreach (var key in _enzymeTermini.Keys)
            {
                if (_enzymeTermini[key].Equals(enzymeTerminiCombo.SelectedItem.ToString()))
                {
                    if (Settings.Default.EnzymeTermini != key)
                    {
                        Settings.Default.EnzymeTermini = key;
                        Parent.SettingsChanged = true;
                    }

                    break;
                }
            }

            if (EnzymeInfoDlg.EnzymeInfoChanged)
            {
                Settings.Default.EnzymeInfo = EnzymeInfo;
                Parent.SettingsChanged = true;
            }

            return true;
        }

        private void InitializeFromDefaultSettings()
        {
            enzymeTerminiCombo.SelectedItem = _enzymeTermini[Settings.Default.EnzymeTermini];

            // For this particular combo, index == value of allowed missed cleavages
            missedCleavagesCombo.SelectedItem = Settings.Default.AllowedMissedCleavages.ToString(CultureInfo.InvariantCulture);


            EnzymeInfo = new StringCollection();
            foreach (var item in Settings.Default.EnzymeInfo)
            {
                EnzymeInfo.Add(item);
            }

            UpdateEnzymeInfo();

            SearchEnzymeCurrentSelectedIndex = Settings.Default.SearchEnzymeNumber;
            SampleEnzymeCurrentSelectedIndex = Settings.Default.SampleEnzymeNumber;

            searchEnzymeCombo.SelectedIndex = SearchEnzymeCurrentSelectedIndex;
            sampleEnzymeCombo.SelectedIndex = SampleEnzymeCurrentSelectedIndex;
        }

        private void UpdateEnzymeInfo()
        {
            sampleEnzymeCombo.Items.Clear();
            searchEnzymeCombo.Items.Clear();

            foreach (var row in EnzymeInfo)
            {
                string[] cells = row.Split(',');

                String sampleEnzymeItem = cells[1] + " (" + cells[3] + "/" + cells[4] + ")";
                sampleEnzymeCombo.Items.Add(sampleEnzymeItem);

                String searchEnzymeItem = cells[1] + " (" + cells[3] + "/" + cells[4] + ")";
                searchEnzymeCombo.Items.Add(searchEnzymeItem);
            }

            // Add the "Edit List" item at the end of the lists
            searchEnzymeCombo.Items.Add("<Edit List...>");
            SearchEnzymeComboEditListIndex = searchEnzymeCombo.Items.Count - 1;
            sampleEnzymeCombo.Items.Add("<Edit List...>");
            SampleEnzymeComboEditListIndex = sampleEnzymeCombo.Items.Count - 1;
        }

        private void SearchEnzymeComboSelectedIndexChanged(object sender, EventArgs e)
        {
            var srchEnzymeCombo = (ComboBox) sender;
            if (SearchEnzymeComboEditListIndex == srchEnzymeCombo.SelectedIndex)
            {
                if ((DialogResult.OK == EnzymeInfoDlg.ShowDialog()) &&
                    EnzymeInfoDlg.EnzymeInfoChanged)
                {
                    UpdateEnzymeInfo();
                }
                else
                {
                    srchEnzymeCombo.SelectedIndex = SearchEnzymeCurrentSelectedIndex;
                }
            }
            else
            {
                SearchEnzymeCurrentSelectedIndex = srchEnzymeCombo.SelectedIndex;
            }
        }

        private void SampleEnzymeComboSelectedIndexChanged(object sender, EventArgs e)
        {
            var smplEnzymeCombo = (ComboBox)sender;
            if (SampleEnzymeComboEditListIndex == smplEnzymeCombo.SelectedIndex)
            {
                if (DialogResult.OK == EnzymeInfoDlg.ShowDialog())
                {
                    if (EnzymeInfoDlg.EnzymeInfoChanged)
                    {
                        UpdateEnzymeInfo();
                    }
                }
                else
                {
                    smplEnzymeCombo.SelectedIndex = SampleEnzymeCurrentSelectedIndex;
                }
            }
            else
            {
                SampleEnzymeCurrentSelectedIndex = smplEnzymeCombo.SelectedIndex;
            }
        }
    }
}
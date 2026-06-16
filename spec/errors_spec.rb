# frozen_string_literal: true

RSpec.describe "Aicoustics.check!" do
  it "returns the code on success (symbol or integer)" do
    expect(Aicoustics.check!(:success)).to eq(:success)
    expect(Aicoustics.check!(0)).to eq(0)
  end

  it "raises a typed error carrying the original code" do
    expect { Aicoustics.check!(:license_expired) }
      .to raise_error(Aicoustics::LicenseExpiredError) { |e| expect(e.code).to eq(:license_expired) }
  end

  it "maps model errors under the ModelError hierarchy" do
    expect { Aicoustics.check!(:model_invalid) }.to raise_error(Aicoustics::ModelError)
  end

  it "maps license errors under the LicenseError hierarchy" do
    expect { Aicoustics.check!(:license_format_invalid) }.to raise_error(Aicoustics::LicenseError)
  end

  it "raises a generic error for unknown codes" do
    expect { Aicoustics.check!(:not_a_real_code) }.to raise_error(Aicoustics::Error)
  end
end
